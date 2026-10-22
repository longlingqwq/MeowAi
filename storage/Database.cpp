#include "storage/Database.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

constexpr int kCurrentSchemaVersion = 4;

/// 读取 meta 中的 schema_version；无记录或非法值视为 0（全新库）
int readSchemaVersion(const QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'"))
        || !query.next()) {
        return 0;
    }
    bool ok = false;
    const int version = query.value(0).toString().toInt(&ok);
    return ok ? version : 0;
}

bool writeSchemaVersion(const QSqlDatabase &db, int version, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO meta (key, value) VALUES ('schema_version', ?)"
                                 " ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(QString::number(version));
    if (!query.exec()) {
        setError(error, QStringLiteral("写入 schema 版本失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool execStatements(const QSqlDatabase &db, const QStringList &statements, QString *error)
{
    for (const QString &sql : statements) {
        QSqlQuery query(db);
        if (!query.exec(sql)) {
            setError(error, QStringLiteral("创建 schema 失败：%1（%2）")
                                .arg(query.lastError().text(), sql.left(64)));
            return false;
        }
    }
    return true;
}

/// v1：会话历史表（功能 #11 之后的阶段三交付）。
/// 资料库相关表不在本批，见 v2。
QStringList schemaV1Statements()
{
    return {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS conversations ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  title      TEXT    NOT NULL DEFAULT '',"
            "  created_at INTEGER NOT NULL,"
            "  updated_at INTEGER NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  conversation_id INTEGER NOT NULL,"
            "  role            TEXT    NOT NULL,"
            "  content         TEXT    NOT NULL DEFAULT '',"
            "  status          TEXT    NOT NULL DEFAULT 'complete',"
            "  created_at      INTEGER NOT NULL,"
            "  FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_messages_conversation"
            "  ON messages(conversation_id, id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS message_attachments ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  message_id INTEGER NOT NULL,"
            "  kind       TEXT    NOT NULL,"
            "  path       TEXT    NOT NULL DEFAULT '',"
            "  mime       TEXT    NOT NULL DEFAULT '',"
            "  FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE"
            ")"),
    };
}

/// `sqlite_master` 里是否已存在同名表 / 虚拟表
bool tableExists(const QSqlDatabase &db, const QString &name)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name = ? LIMIT 1"));
    query.addBindValue(name);
    return query.exec() && query.next();
}

/// 表是否已有指定列（SQLite 的 ADD COLUMN 没有 IF NOT EXISTS，迁移必须自行判重）
bool columnExists(const QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (query.next()) {
        if (query.value(1).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/// 幂等补列：已存在则跳过；失败返回 false + error，不删除任何旧数据
bool ensureColumn(const QSqlDatabase &db, const QString &table, const QString &column,
                  const QString &definition, QString *error)
{
    if (columnExists(db, table, column))
        return true;

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                        .arg(table, column, definition))) {
        setError(error, QStringLiteral("数据库升级失败（%1.%2）：%3")
                            .arg(table, column, query.lastError().text()));
        return false;
    }
    return true;
}

/// 探测 FTS5 是否可用（Qt 自带的 SQLite 可能未编译 FTS5）。
/// 以 `sqlite_compileoption_used` 为准，再回落编译选项列表扫描；两者都拿不到即为不可用。
bool probeFts5(const QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT sqlite_compileoption_used('ENABLE_FTS5')"))
        && query.next()) {
        return query.value(0).toInt() == 1;
    }

    QSqlQuery options(db);
    if (options.exec(QStringLiteral("PRAGMA compile_options"))) {
        while (options.next()) {
            const QString option = options.value(0).toString().trimmed().toUpper();
            if (option == QStringLiteral("ENABLE_FTS5") || option.startsWith(QStringLiteral("ENABLE_FTS5="))) {
                return true;
            }
        }
    }
    return false;
}

/// v2：资料库（小库 / 文件 / 归库关系 / 文件夹快照 / 片段 / 向量 / 来源）。
/// 只新增表与索引，**不触碰 v1 的会话历史表**；全部 `IF NOT EXISTS`，重复执行安全。
/// `chunk_fts`（虚拟表）不在此列表内：需先探测 FTS5 能力，见 ensureSchema。
QStringList schemaV2Statements()
{
    return {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS collections ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name        TEXT    NOT NULL,"
            "  description TEXT    NOT NULL DEFAULT '',"
            "  created_at  INTEGER NOT NULL,"
            "  updated_at  INTEGER NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_collections_name ON collections(name)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS library_files ("
            "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  display_name  TEXT    NOT NULL,"
            "  source_kind   TEXT    NOT NULL,"            // upload | folder
            "  source_path   TEXT    NOT NULL,"            // upload=库内副本 / folder=用户源文件路径
            "  origin_path   TEXT    NOT NULL DEFAULT ''," // upload 原路径（仅记录，绝不删除）
            "  folder_link_id INTEGER,"
            "  ext           TEXT    NOT NULL DEFAULT '',"
            "  kind          TEXT    NOT NULL,"            // text | image | video
            "  size_bytes    INTEGER NOT NULL DEFAULT 0,"
            "  mtime         INTEGER NOT NULL DEFAULT 0,"
            "  content_hash  TEXT    NOT NULL DEFAULT '',"
            "  status        TEXT    NOT NULL DEFAULT 'pending',"
            "  error_text    TEXT    NOT NULL DEFAULT '',"
            "  chunk_count   INTEGER NOT NULL DEFAULT 0,"
            "  indexed_at    INTEGER NOT NULL DEFAULT 0,"
            "  created_at    INTEGER NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_files_source ON library_files(source_path)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_files_status ON library_files(status)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS file_collections ("
            "  file_id       INTEGER NOT NULL,"
            "  collection_id INTEGER NOT NULL,"
            "  added_at      INTEGER NOT NULL,"
            "  PRIMARY KEY (file_id, collection_id),"
            "  FOREIGN KEY (file_id) REFERENCES library_files(id) ON DELETE CASCADE,"
            "  FOREIGN KEY (collection_id) REFERENCES collections(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS folder_links ("
            "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  collection_id INTEGER NOT NULL,"
            "  root_path     TEXT    NOT NULL,"
            "  snapshot_at   INTEGER NOT NULL DEFAULT 0,"
            "  file_count    INTEGER NOT NULL DEFAULT 0,"
            "  status        TEXT    NOT NULL DEFAULT 'empty',"
            "  error_text    TEXT    NOT NULL DEFAULT '',"
            "  FOREIGN KEY (collection_id) REFERENCES collections(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS chunks ("
            "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  file_id      INTEGER NOT NULL,"
            "  ordinal      INTEGER NOT NULL,"            // 文件内顺序（邻居扩展依赖）
            "  text         TEXT    NOT NULL,"
            "  loc_kind     TEXT    NOT NULL DEFAULT '',"
            "  loc_label    TEXT    NOT NULL DEFAULT '',"
            "  loc_page     INTEGER NOT NULL DEFAULT 0,"
            "  loc_sheet    TEXT    NOT NULL DEFAULT '',"
            "  loc_row_from INTEGER NOT NULL DEFAULT 0,"
            "  loc_row_to   INTEGER NOT NULL DEFAULT 0,"
            "  char_count   INTEGER NOT NULL DEFAULT 0,"
            "  token_est    INTEGER NOT NULL DEFAULT 0,"
            "  created_at   INTEGER NOT NULL,"
            "  FOREIGN KEY (file_id) REFERENCES library_files(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_chunks_file ON chunks(file_id, ordinal)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS chunk_vectors ("
            "  chunk_id    INTEGER PRIMARY KEY,"
            "  provider_id TEXT    NOT NULL,"
            "  model       TEXT    NOT NULL,"
            "  dim         INTEGER NOT NULL,"
            "  vector      BLOB    NOT NULL,"
            "  created_at  INTEGER NOT NULL,"
            "  FOREIGN KEY (chunk_id) REFERENCES chunks(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_vectors_model ON chunk_vectors(provider_id, model, dim)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS citations ("
            "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  message_id      INTEGER NOT NULL DEFAULT 0,"
            "  conversation_id INTEGER NOT NULL DEFAULT 0,"
            "  chunk_id        INTEGER NOT NULL,"
            "  file_id         INTEGER NOT NULL,"
            "  collection_id   INTEGER NOT NULL DEFAULT 0,"
            "  loc_label       TEXT    NOT NULL DEFAULT '',"
            "  rank            INTEGER NOT NULL DEFAULT 0,"
            "  score           REAL    NOT NULL DEFAULT 0,"
            "  created_at      INTEGER NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_citations_message ON citations(message_id, rank)"),
    };
}

/// 建立 FTS5 关键词索引（trigram 分词器支持中文子串匹配）。
/// 失败时逐级降级：trigram → 默认分词器 → 关闭 FTS（关键词检索改走 LIKE），
/// 绝不因为虚拟表建不出来而让整个 schema 迁移失败。
bool createChunkFts(const QSqlDatabase &db, bool *ftsAvailable)
{
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS chunk_fts"
                                  " USING fts5(text, tokenize='trigram')"))) {
        *ftsAvailable = true;
        return true;
    }

    QSqlQuery fallback(db);
    if (fallback.exec(QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS chunk_fts"
                                     " USING fts5(text)"))) {
        *ftsAvailable = true;
        return true;
    }

    // 降级不算致命错误：关键词检索改走 LIKE，库内其余能力不受影响。
    *ftsAvailable = false;
    qWarning("MeowAi: chunk_fts 虚拟表创建失败（%s），关键词检索降级为 LIKE",
             qPrintable(fallback.lastError().text()));
    return true;
}

/// v3：参考资料模式（文件级召回 + 章节注入）。
/// 新增 documents（文档元数据，token 估算 + 章节来源类型）与 sections（章节结构）两表。
/// 全部 IF NOT EXISTS，与 v1/v2 表完全解耦；旧文件在这两表无行时自动回退到片段模式。
QStringList schemaV3Statements()
{
    return {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS documents ("
            "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  file_id         INTEGER NOT NULL UNIQUE,"
            "  token_est_total INTEGER NOT NULL DEFAULT 0,"
            "  section_source  TEXT    NOT NULL DEFAULT 'none',"
            "  section_count   INTEGER NOT NULL DEFAULT 0,"
            "  built_at        INTEGER NOT NULL DEFAULT 0,"
            "  FOREIGN KEY (file_id) REFERENCES library_files(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_documents_file ON documents(file_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sections ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  document_id INTEGER NOT NULL,"
            "  ordinal     INTEGER NOT NULL,"
            "  title       TEXT    NOT NULL DEFAULT '',"
            "  chunk_start INTEGER NOT NULL DEFAULT 0,"
            "  chunk_end   INTEGER NOT NULL DEFAULT 0,"
            "  token_est   INTEGER NOT NULL DEFAULT 0,"
            "  FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sections_doc ON sections(document_id, ordinal)"),
    };
}

} // namespace

Database::Database()
    : m_connectionName(QStringLiteral("meowai_%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &dbPath, QString *error)
{
    if (dbPath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("数据库路径为空"));
        return false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(error, QStringLiteral("缺少 QSQLITE 驱动，无法打开数据库"));
        return false;
    }

    const QFileInfo info(dbPath);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath()));
        return false;
    }

    close();

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        const QString reason = db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        setError(error, QStringLiteral("打开数据库失败：%1").arg(reason));
        return false;
    }

    m_open = true;
    return true;
}

void Database::close()
{
    if (!m_open) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, /*open=*/false);
        if (db.isValid() && db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

bool Database::isOpen() const
{
    return m_open;
}

bool Database::ensureSchema(QString *error)
{
    if (!m_open) {
        setError(error, QStringLiteral("数据库尚未打开"));
        return false;
    }

    if (error) {
        error->clear();
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, /*open=*/false);

    // 连接级 PRAGMA：外键级联删除、WAL 并发读写、忙等待避免瞬时锁冲突。
    // 失败不视为致命错误（部分构建的 SQLite 可能不支持 WAL），仅继续走后续步骤。
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 3000"));

    // FTS5 能力探测必须在版本提前返回之前完成：库已是 v2 时 `chunk_fts` 早已存在，
    // 但本次连接仍需知道该用 MATCH 还是降级 LIKE。
    m_fts5Available = probeFts5(db);

    if (!execStatements(db, { QStringLiteral("CREATE TABLE IF NOT EXISTS meta ("
                                             "  key   TEXT PRIMARY KEY,"
                                             "  value TEXT"
                                             ")") },
                        error)) {
        return false;
    }

    // 幂等前向迁移：仅当版本落后才执行对应增量，绝不回退或删除既有表。
    const int version = readSchemaVersion(db);
    if (version < 1) {
        if (!execStatements(db, schemaV1Statements(), error)) {
            return false;
        }
    }
    if (version < 2) {
        if (!execStatements(db, schemaV2Statements(), error)) {
            return false;
        }
    }
    if (version < 3) {
        if (!execStatements(db, schemaV3Statements(), error)) {
            return false;
        }
    }
    if (version < 4) {
        // v4：消息 usage 与耗时。旧库自动补列；已存在则跳过（幂等）。
        // 失败不崩溃、不删旧数据，只返回错误并由上层提示。
        if (!ensureColumn(db, QStringLiteral("messages"), QStringLiteral("total_tokens"),
                          QStringLiteral("INTEGER NOT NULL DEFAULT -1"), error)
            || !ensureColumn(db, QStringLiteral("messages"), QStringLiteral("prompt_tokens"),
                             QStringLiteral("INTEGER NOT NULL DEFAULT -1"), error)
            || !ensureColumn(db, QStringLiteral("messages"), QStringLiteral("completion_tokens"),
                             QStringLiteral("INTEGER NOT NULL DEFAULT -1"), error)
            || !ensureColumn(db, QStringLiteral("messages"), QStringLiteral("elapsed_ms"),
                             QStringLiteral("INTEGER NOT NULL DEFAULT -1"), error)) {
            return false;
        }
    }

    // FTS 索引表与版本号解耦：能力可用但表缺失（如迁移时 FTS5 尚未启用）时补建，
    // 已在 v2 的库同样会被自愈，避免 LibraryStore 写入不存在的虚拟表。
    if (m_fts5Available && !tableExists(db, QStringLiteral("chunk_fts"))) {
        createChunkFts(db, &m_fts5Available);
    }

    if (version >= kCurrentSchemaVersion) {
        return true;
    }
    return writeSchemaVersion(db, kCurrentSchemaVersion, error);
}

bool Database::fts5Available() const
{
    return m_fts5Available;
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connectionName, /*open=*/false);
}
