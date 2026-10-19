#include "storage/Database.h"

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

constexpr int kCurrentSchemaVersion = 1;

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
/// 为后续「资料库与分类」模块预留位，但**不预建**其表，避免占位表与真实结构冲突。
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

    if (!execStatements(db, { QStringLiteral("CREATE TABLE IF NOT EXISTS meta ("
                                             "  key   TEXT PRIMARY KEY,"
                                             "  value TEXT"
                                             ")") },
                        error)) {
        return false;
    }

    // 幂等前向迁移：仅当版本落后才执行对应增量，绝不回退或删除既有表。
    const int version = readSchemaVersion(db);
    if (version >= kCurrentSchemaVersion) {
        return true;
    }

    if (version < 1) {
        if (!execStatements(db, schemaV1Statements(), error)) {
            return false;
        }
    }

    return writeSchemaVersion(db, kCurrentSchemaVersion, error);
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connectionName, /*open=*/false);
}
