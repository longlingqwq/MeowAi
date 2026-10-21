#include "storage/LibraryStore.h"

#include "storage/Database.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// SQLite 的 NOT NULL 列不接受 NULL，而 `QString()` 是 null 字符串、会被绑定成 NULL。
QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

/// 可空列的绑定：0 视为「未关联」写入 NULL
QVariant nullableId(qint64 value)
{
    return value > 0 ? QVariant(value) : QVariant();
}

QString placeholders(int count)
{
    QStringList list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        list.append(QLatin1String("?"));
    }
    return list.join(QLatin1Char(','));
}

const QString kCollectionColumns = QStringLiteral("id, name, description, created_at, updated_at");
const QString kFileColumns = QStringLiteral(
    "id, display_name, source_kind, source_path, origin_path, folder_link_id, ext, kind,"
    " size_bytes, mtime, content_hash, status, error_text, chunk_count, indexed_at, created_at");
const QString kFolderLinkColumns = QStringLiteral(
    "id, collection_id, root_path, snapshot_at, file_count, status, error_text");
const QString kChunkColumns = QStringLiteral(
    "id, file_id, ordinal, text, loc_kind, loc_label, loc_page, loc_sheet, loc_row_from,"
    " loc_row_to, char_count, token_est, created_at");
const QString kCitationColumns = QStringLiteral(
    "id, message_id, conversation_id, chunk_id, file_id, collection_id, loc_label, rank, score,"
    " created_at");

CollectionRecord readCollection(const QSqlQuery &query)
{
    CollectionRecord record;
    record.id = query.value(0).toLongLong();
    record.name = query.value(1).toString();
    record.description = query.value(2).toString();
    record.createdAt = query.value(3).toLongLong();
    record.updatedAt = query.value(4).toLongLong();
    return record;
}

LibraryFileRecord readFile(const QSqlQuery &query)
{
    LibraryFileRecord record;
    record.id = query.value(0).toLongLong();
    record.displayName = query.value(1).toString();
    record.sourceKind = query.value(2).toString();
    record.sourcePath = query.value(3).toString();
    record.originPath = query.value(4).toString();
    record.folderLinkId = query.value(5).toLongLong();
    record.ext = query.value(6).toString();
    record.kind = query.value(7).toString();
    record.sizeBytes = query.value(8).toLongLong();
    record.mtime = query.value(9).toLongLong();
    record.contentHash = query.value(10).toString();
    record.status = query.value(11).toString();
    record.errorText = query.value(12).toString();
    record.chunkCount = query.value(13).toInt();
    record.indexedAt = query.value(14).toLongLong();
    record.createdAt = query.value(15).toLongLong();
    return record;
}

FolderLinkRecord readFolderLink(const QSqlQuery &query)
{
    FolderLinkRecord record;
    record.id = query.value(0).toLongLong();
    record.collectionId = query.value(1).toLongLong();
    record.rootPath = query.value(2).toString();
    record.snapshotAt = query.value(3).toLongLong();
    record.fileCount = query.value(4).toInt();
    record.status = query.value(5).toString();
    record.errorText = query.value(6).toString();
    return record;
}

ChunkRecord readChunk(const QSqlQuery &query)
{
    ChunkRecord record;
    record.id = query.value(0).toLongLong();
    record.fileId = query.value(1).toLongLong();
    record.ordinal = query.value(2).toInt();
    record.text = query.value(3).toString();
    record.locKind = query.value(4).toString();
    record.locLabel = query.value(5).toString();
    record.locPage = query.value(6).toInt();
    record.locSheet = query.value(7).toString();
    record.locRowFrom = query.value(8).toInt();
    record.locRowTo = query.value(9).toInt();
    record.charCount = query.value(10).toInt();
    record.tokenEst = query.value(11).toInt();
    record.createdAt = query.value(12).toLongLong();
    return record;
}

CitationRecord readCitation(const QSqlQuery &query)
{
    CitationRecord record;
    record.id = query.value(0).toLongLong();
    record.messageId = query.value(1).toLongLong();
    record.conversationId = query.value(2).toLongLong();
    record.chunkId = query.value(3).toLongLong();
    record.fileId = query.value(4).toLongLong();
    record.collectionId = query.value(5).toLongLong();
    record.locLabel = query.value(6).toString();
    record.rank = query.value(7).toInt();
    record.score = query.value(8).toDouble();
    record.createdAt = query.value(9).toLongLong();
    return record;
}

}  // namespace

LibraryStore::LibraryStore(Database *database)
    : m_database(database)
{
}

bool LibraryStore::isAvailable() const
{
    return m_database && m_database->isOpen();
}

bool LibraryStore::ftsAvailable() const
{
    return m_database && m_database->fts5Available();
}

// ---------------------------------------------------------------------- 小库

QList<CollectionRecord> LibraryStore::collections(QString *error) const
{
    QList<CollectionRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取小库列表"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    if (!query.exec(QStringLiteral("SELECT %1 FROM collections"
                                   " ORDER BY name COLLATE NOCASE ASC, id ASC")
                        .arg(kCollectionColumns))) {
        setError(error, QStringLiteral("读取小库列表失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readCollection(query));
    }
    return result;
}

bool LibraryStore::collectionByName(const QString &name, CollectionRecord *out, QString *error) const
{
    if (out) {
        *out = CollectionRecord();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法查询小库"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM collections WHERE name = ? LIMIT 1")
                      .arg(kCollectionColumns));
    query.addBindValue(nonNullText(name));
    if (!query.exec()) {
        setError(error, QStringLiteral("查询小库失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (!query.next()) {
        return false;
    }
    if (out) {
        *out = readCollection(query);
    }
    return true;
}

bool LibraryStore::insertCollection(const QString &name, const QString &description, qint64 *newId,
                                    QString *error) const
{
    if (newId) {
        *newId = 0;
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法新建小库"));
        return false;
    }
    if (name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("小库名称不能为空"));
        return false;
    }

    CollectionRecord existing;
    if (collectionByName(name, &existing, nullptr)) {
        setError(error, QStringLiteral("小库名称已存在：%1").arg(name));
        return false;
    }

    const qint64 now = nowMillis();
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("INSERT INTO collections (name, description, created_at, updated_at)"
                                 " VALUES (?, ?, ?, ?)"));
    query.addBindValue(nonNullText(name));
    query.addBindValue(nonNullText(description));
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        setError(error, QStringLiteral("新建小库失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (newId) {
        *newId = query.lastInsertId().toLongLong();
    }
    return true;
}

bool LibraryStore::updateCollection(qint64 collectionId, const QString &name,
                                    const QString &description, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法修改小库"));
        return false;
    }
    if (collectionId <= 0 || name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("小库 id 或名称为空，无法修改"));
        return false;
    }

    CollectionRecord existing;
    if (collectionByName(name, &existing, nullptr) && existing.id != collectionId) {
        setError(error, QStringLiteral("小库名称已存在：%1").arg(name));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("UPDATE collections SET name = ?, description = ?, updated_at = ?"
                                 " WHERE id = ?"));
    query.addBindValue(nonNullText(name));
    query.addBindValue(nonNullText(description));
    query.addBindValue(nowMillis());
    query.addBindValue(collectionId);
    if (!query.exec()) {
        setError(error, QStringLiteral("修改小库失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool LibraryStore::removeCollection(qint64 collectionId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法删除小库"));
        return false;
    }

    // 只删小库记录：file_collections / folder_links 经外键级联清理；
    // library_files 与磁盘上的真实文件**都不受影响**（文件可同时属于其他小库）。
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM collections WHERE id = ?"));
    query.addBindValue(collectionId);
    if (!query.exec()) {
        setError(error, QStringLiteral("删除小库失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------- 文件

QList<LibraryFileRecord> LibraryStore::allFiles(QString *error) const
{
    QList<LibraryFileRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取文件列表"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    if (!query.exec(QStringLiteral("SELECT %1 FROM library_files ORDER BY id ASC").arg(kFileColumns))) {
        setError(error, QStringLiteral("读取文件列表失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readFile(query));
    }
    return result;
}

QList<LibraryFileRecord> LibraryStore::filesInCollection(qint64 collectionId, QString *error) const
{
    QList<LibraryFileRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取库内文件"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM library_files"
                                 " WHERE id IN (SELECT file_id FROM file_collections"
                                 " WHERE collection_id = ?) ORDER BY id ASC")
                      .arg(kFileColumns));
    query.addBindValue(collectionId);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取库内文件失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readFile(query));
    }
    return result;
}

QList<LibraryFileRecord> LibraryStore::filesByFolderLink(qint64 folderLinkId, QString *error) const
{
    QList<LibraryFileRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取关联文件夹下的文件"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM library_files WHERE folder_link_id = ?"
                                 " ORDER BY id ASC")
                      .arg(kFileColumns));
    query.addBindValue(folderLinkId);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取关联文件夹下的文件失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readFile(query));
    }
    return result;
}

bool LibraryStore::fileById(qint64 fileId, LibraryFileRecord *out, QString *error) const
{
    if (out) {
        *out = LibraryFileRecord();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法查询文件"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM library_files WHERE id = ?").arg(kFileColumns));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("查询文件失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (!query.next()) {
        return false;
    }
    if (out) {
        *out = readFile(query);
    }
    return true;
}

bool LibraryStore::fileBySourcePath(const QString &sourcePath, LibraryFileRecord *out,
                                    QString *error) const
{
    if (out) {
        *out = LibraryFileRecord();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法查询文件"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM library_files WHERE source_path = ? LIMIT 1")
                      .arg(kFileColumns));
    query.addBindValue(nonNullText(sourcePath));
    if (!query.exec()) {
        setError(error, QStringLiteral("查询文件失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (!query.next()) {
        return false;
    }
    if (out) {
        *out = readFile(query);
    }
    return true;
}

bool LibraryStore::insertFile(const LibraryFileRecord &record, qint64 *newId, QString *error) const
{
    if (newId) {
        *newId = 0;
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法写入文件"));
        return false;
    }
    if (record.sourcePath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("文件来源路径为空，无法入库"));
        return false;
    }

    LibraryFileRecord existing;
    if (fileBySourcePath(record.sourcePath, &existing, nullptr)) {
        setError(error, QStringLiteral("该来源路径已在资料库中：%1").arg(record.sourcePath));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "INSERT INTO library_files (display_name, source_kind, source_path, origin_path,"
        " folder_link_id, ext, kind, size_bytes, mtime, content_hash, status, error_text,"
        " chunk_count, indexed_at, created_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(nonNullText(record.displayName));
    query.addBindValue(nonNullText(record.sourceKind));
    query.addBindValue(nonNullText(record.sourcePath));
    query.addBindValue(nonNullText(record.originPath));
    query.addBindValue(nullableId(record.folderLinkId));
    query.addBindValue(nonNullText(record.ext));
    query.addBindValue(nonNullText(record.kind));
    query.addBindValue(record.sizeBytes);
    query.addBindValue(record.mtime);
    query.addBindValue(nonNullText(record.contentHash));
    query.addBindValue(record.status.isEmpty() ? QStringLiteral("pending") : record.status);
    query.addBindValue(nonNullText(record.errorText));
    query.addBindValue(record.chunkCount);
    query.addBindValue(record.indexedAt);
    query.addBindValue(record.createdAt > 0 ? record.createdAt : nowMillis());
    if (!query.exec()) {
        setError(error, QStringLiteral("写入文件失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (newId) {
        *newId = query.lastInsertId().toLongLong();
    }
    return true;
}

bool LibraryStore::updateFile(const LibraryFileRecord &record, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法更新文件"));
        return false;
    }
    if (record.id <= 0) {
        setError(error, QStringLiteral("文件 id 无效，无法更新"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "UPDATE library_files SET display_name = ?, source_kind = ?, source_path = ?,"
        " origin_path = ?, folder_link_id = ?, ext = ?, kind = ?, size_bytes = ?, mtime = ?,"
        " content_hash = ?, status = ?, error_text = ?, chunk_count = ?, indexed_at = ?"
        " WHERE id = ?"));
    query.addBindValue(nonNullText(record.displayName));
    query.addBindValue(nonNullText(record.sourceKind));
    query.addBindValue(nonNullText(record.sourcePath));
    query.addBindValue(nonNullText(record.originPath));
    query.addBindValue(nullableId(record.folderLinkId));
    query.addBindValue(nonNullText(record.ext));
    query.addBindValue(nonNullText(record.kind));
    query.addBindValue(record.sizeBytes);
    query.addBindValue(record.mtime);
    query.addBindValue(nonNullText(record.contentHash));
    query.addBindValue(record.status);
    query.addBindValue(nonNullText(record.errorText));
    query.addBindValue(record.chunkCount);
    query.addBindValue(record.indexedAt);
    query.addBindValue(record.id);
    if (!query.exec()) {
        setError(error, QStringLiteral("更新文件失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool LibraryStore::removeFile(qint64 fileId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法删除文件记录"));
        return false;
    }

    QSqlDatabase db = m_database->handle();
    db.transaction();

    // FTS 虚拟表无外键，必须在片段被级联删除之前先清掉它的索引行
    if (ftsAvailable()) {
        QSqlQuery fts(db);
        fts.prepare(QStringLiteral("DELETE FROM chunk_fts WHERE rowid IN"
                                   " (SELECT id FROM chunks WHERE file_id = ?)"));
        fts.addBindValue(fileId);
        if (!fts.exec()) {
            db.rollback();
            setError(error, QStringLiteral("清理关键词索引失败：%1").arg(fts.lastError().text()));
            return false;
        }
    }

    // 仅删索引记录：chunks / chunk_vectors 经外键级联清理，真实文件不在本层职责内
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM library_files WHERE id = ?"));
    query.addBindValue(fileId);
    if (!query.exec()) {
        db.rollback();
        setError(error, QStringLiteral("删除文件记录失败：%1").arg(query.lastError().text()));
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        setError(error, QStringLiteral("提交删除文件失败：%1").arg(db.lastError().text()));
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ 归库关系

bool LibraryStore::linkFileToCollection(qint64 fileId, qint64 collectionId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法关联小库"));
        return false;
    }

    // 重复关联是幂等的：INSERT OR IGNORE 让「同一文件加入同一库两次」静默成功
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO file_collections"
                                 " (file_id, collection_id, added_at) VALUES (?, ?, ?)"));
    query.addBindValue(fileId);
    query.addBindValue(collectionId);
    query.addBindValue(nowMillis());
    if (!query.exec()) {
        setError(error, QStringLiteral("关联文件到小库失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool LibraryStore::unlinkFileFromCollection(qint64 fileId, qint64 collectionId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法解除关联"));
        return false;
    }

    // 只删归库关系，**文件记录与真实文件都保留**
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM file_collections"
                                 " WHERE file_id = ? AND collection_id = ?"));
    query.addBindValue(fileId);
    query.addBindValue(collectionId);
    if (!query.exec()) {
        setError(error, QStringLiteral("解除文件与小库的关联失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<qint64> LibraryStore::collectionIdsOfFile(qint64 fileId, QString *error) const
{
    QList<qint64> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取文件所属小库"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT collection_id FROM file_collections WHERE file_id = ?"
                                 " ORDER BY collection_id ASC"));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取文件所属小库失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(query.value(0).toLongLong());
    }
    return result;
}

// -------------------------------------------------------------- 关联文件夹快照

QList<FolderLinkRecord> LibraryStore::folderLinks(qint64 collectionId, QString *error) const
{
    QList<FolderLinkRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取关联文件夹"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    if (collectionId > 0) {
        query.prepare(QStringLiteral("SELECT %1 FROM folder_links WHERE collection_id = ?"
                                     " ORDER BY id ASC")
                          .arg(kFolderLinkColumns));
        query.addBindValue(collectionId);
    } else {
        query.prepare(QStringLiteral("SELECT %1 FROM folder_links ORDER BY id ASC")
                          .arg(kFolderLinkColumns));
    }
    if (!query.exec()) {
        setError(error, QStringLiteral("读取关联文件夹失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readFolderLink(query));
    }
    return result;
}

bool LibraryStore::insertFolderLink(const FolderLinkRecord &record, qint64 *newId,
                                    QString *error) const
{
    if (newId) {
        *newId = 0;
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法写入关联文件夹"));
        return false;
    }
    if (record.collectionId <= 0 || record.rootPath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("关联文件夹缺少所属小库或路径"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "INSERT INTO folder_links (collection_id, root_path, snapshot_at, file_count, status,"
        " error_text) VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(record.collectionId);
    query.addBindValue(nonNullText(record.rootPath));
    query.addBindValue(record.snapshotAt);
    query.addBindValue(record.fileCount);
    query.addBindValue(record.status.isEmpty() ? QStringLiteral("empty") : record.status);
    query.addBindValue(nonNullText(record.errorText));
    if (!query.exec()) {
        setError(error, QStringLiteral("写入关联文件夹失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (newId) {
        *newId = query.lastInsertId().toLongLong();
    }
    return true;
}

bool LibraryStore::updateFolderLinkSnapshot(qint64 linkId, qint64 snapshotAt, int fileCount,
                                            const QString &status, const QString &errorText,
                                            QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法更新关联文件夹"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("UPDATE folder_links SET snapshot_at = ?, file_count = ?,"
                                 " status = ?, error_text = ? WHERE id = ?"));
    query.addBindValue(snapshotAt);
    query.addBindValue(fileCount);
    query.addBindValue(nonNullText(status));
    query.addBindValue(nonNullText(errorText));
    query.addBindValue(linkId);
    if (!query.exec()) {
        setError(error, QStringLiteral("更新关联文件夹快照失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool LibraryStore::removeFolderLink(qint64 linkId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法解除关联文件夹"));
        return false;
    }

    // 只删关联记录；其下文件的索引记录由 core 层按需逐个 removeFile（真实文件不删）
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM folder_links WHERE id = ?"));
    query.addBindValue(linkId);
    if (!query.exec()) {
        setError(error, QStringLiteral("解除关联文件夹失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------- 片段

bool LibraryStore::replaceChunks(qint64 fileId, const QList<ChunkRecord> &chunks, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法写入片段"));
        return false;
    }
    if (fileId <= 0) {
        setError(error, QStringLiteral("文件 id 无效，无法写入片段"));
        return false;
    }

    QSqlDatabase db = m_database->handle();
    db.transaction();

    if (ftsAvailable()) {
        QSqlQuery fts(db);
        fts.prepare(QStringLiteral("DELETE FROM chunk_fts WHERE rowid IN"
                                   " (SELECT id FROM chunks WHERE file_id = ?)"));
        fts.addBindValue(fileId);
        if (!fts.exec()) {
            db.rollback();
            setError(error, QStringLiteral("清理旧关键词索引失败：%1").arg(fts.lastError().text()));
            return false;
        }
    }

    // chunks 被删时 chunk_vectors 经外键级联清理：换模型重建向量时必须重新切块入库
    QSqlQuery clear(db);
    clear.prepare(QStringLiteral("DELETE FROM chunks WHERE file_id = ?"));
    clear.addBindValue(fileId);
    if (!clear.exec()) {
        db.rollback();
        setError(error, QStringLiteral("清理旧片段失败：%1").arg(clear.lastError().text()));
        return false;
    }

    const qint64 now = nowMillis();
    int inserted = 0;
    for (const ChunkRecord &chunk : chunks) {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral(
            "INSERT INTO chunks (file_id, ordinal, text, loc_kind, loc_label, loc_page, loc_sheet,"
            " loc_row_from, loc_row_to, char_count, token_est, created_at)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(fileId);
        insert.addBindValue(chunk.ordinal);
        insert.addBindValue(nonNullText(chunk.text));
        insert.addBindValue(nonNullText(chunk.locKind));
        insert.addBindValue(nonNullText(chunk.locLabel));
        insert.addBindValue(chunk.locPage);
        insert.addBindValue(nonNullText(chunk.locSheet));
        insert.addBindValue(chunk.locRowFrom);
        insert.addBindValue(chunk.locRowTo);
        insert.addBindValue(chunk.charCount);
        insert.addBindValue(chunk.tokenEst);
        insert.addBindValue(chunk.createdAt > 0 ? chunk.createdAt : now);
        if (!insert.exec()) {
            db.rollback();
            setError(error, QStringLiteral("写入片段失败：%1").arg(insert.lastError().text()));
            return false;
        }
        ++inserted;

        if (ftsAvailable()) {
            QSqlQuery ftsInsert(db);
            ftsInsert.prepare(QStringLiteral("INSERT INTO chunk_fts (rowid, text) VALUES (?, ?)"));
            ftsInsert.addBindValue(insert.lastInsertId().toLongLong());
            ftsInsert.addBindValue(nonNullText(chunk.text));
            if (!ftsInsert.exec()) {
                db.rollback();
                setError(error, QStringLiteral("写入关键词索引失败：%1")
                                    .arg(ftsInsert.lastError().text()));
                return false;
            }
        }
    }

    QSqlQuery count(db);
    count.prepare(QStringLiteral("UPDATE library_files SET chunk_count = ? WHERE id = ?"));
    count.addBindValue(inserted);
    count.addBindValue(fileId);
    if (!count.exec()) {
        db.rollback();
        setError(error, QStringLiteral("回填片段数量失败：%1").arg(count.lastError().text()));
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        setError(error, QStringLiteral("提交片段写入失败：%1").arg(db.lastError().text()));
        return false;
    }
    return true;
}

QList<ChunkRecord> LibraryStore::chunksOfFile(qint64 fileId, QString *error) const
{
    QList<ChunkRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取片段"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM chunks WHERE file_id = ? ORDER BY ordinal ASC")
                      .arg(kChunkColumns));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取片段失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readChunk(query));
    }
    return result;
}

QList<ChunkRecord> LibraryStore::chunksByIds(const QList<qint64> &chunkIds, QString *error) const
{
    QList<ChunkRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取片段"));
        return result;
    }
    if (chunkIds.isEmpty()) {
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM chunks WHERE id IN (%2)"
                                 " ORDER BY file_id ASC, ordinal ASC")
                      .arg(kChunkColumns, placeholders(chunkIds.size())));
    for (qint64 id : chunkIds) {
        query.addBindValue(id);
    }
    if (!query.exec()) {
        setError(error, QStringLiteral("读取片段失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readChunk(query));
    }
    return result;
}

bool LibraryStore::chunkById(qint64 chunkId, ChunkRecord *out, QString *error) const
{
    if (out) {
        *out = ChunkRecord();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法查询片段"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM chunks WHERE id = ?").arg(kChunkColumns));
    query.addBindValue(chunkId);
    if (!query.exec()) {
        setError(error, QStringLiteral("查询片段失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (!query.next()) {
        return false;
    }
    if (out) {
        *out = readChunk(query);
    }
    return true;
}

QList<ChunkRecord> LibraryStore::chunksInOrdinalRange(qint64 fileId, int ordinalFrom, int ordinalTo,
                                                      QString *error) const
{
    QList<ChunkRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取相邻片段"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM chunks WHERE file_id = ? AND ordinal >= ?"
                                 " AND ordinal <= ? ORDER BY ordinal ASC")
                      .arg(kChunkColumns));
    query.addBindValue(fileId);
    query.addBindValue(ordinalFrom);
    query.addBindValue(ordinalTo);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取相邻片段失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readChunk(query));
    }
    return result;
}

// ---------------------------------------------------------- 关键词检索（兜底）

QList<qint64> LibraryStore::searchChunkIds(const QString &query, const QList<qint64> &collectionIds,
                                           int limit, QString *error) const
{
    QList<qint64> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法进行关键词检索"));
        return result;
    }

    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        setError(error, QStringLiteral("关键词为空，无法检索"));
        return result;
    }

    // 库过滤：空集合表示在全部已入库内容中检索
    QString collectionFilter;
    if (!collectionIds.isEmpty()) {
        collectionFilter = QStringLiteral(" AND file_id IN (SELECT file_id FROM file_collections"
                                          " WHERE collection_id IN (%1))")
                               .arg(placeholders(collectionIds.size()));
    }

    // trigram 分词器至少要 3 个字符才能成词，更短的查询直接走 LIKE
    const bool useFts = ftsAvailable() && needle.size() >= 3;
    if (useFts) {
        QString escaped = needle;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        const QString phrase = QLatin1Char('"') + escaped + QLatin1Char('"');

        QSqlQuery query(m_database->handle());
        QString sql = QStringLiteral("SELECT chunks.id FROM chunk_fts"
                                     " JOIN chunks ON chunks.id = chunk_fts.rowid"
                                     " WHERE chunk_fts MATCH ?");
        sql += collectionFilter.isEmpty()
                   ? QString()
                   : QStringLiteral(" AND chunks.file_id IN (SELECT file_id FROM file_collections"
                                    " WHERE collection_id IN (%1))")
                         .arg(placeholders(collectionIds.size()));
        sql += QStringLiteral(" ORDER BY bm25(chunk_fts) ASC");
        if (limit > 0) {
            sql += QStringLiteral(" LIMIT ?");
        }
        query.prepare(sql);
        query.addBindValue(phrase);
        for (qint64 id : collectionIds) {
            query.addBindValue(id);
        }
        if (limit > 0) {
            query.addBindValue(limit);
        }
        if (query.exec()) {
            while (query.next()) {
                result.append(query.value(0).toLongLong());
            }
            return result;
        }
        // 任意标点 / 特殊字符可能让 MATCH 语法失败：降级 LIKE，不让检索整体报错
        qWarning("MeowAi: 关键词 MATCH 失败（%s），本次降级为 LIKE",
                 qPrintable(query.lastError().text()));
    }

    QString escaped = needle;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));

    QSqlQuery like(m_database->handle());
    QString sql = QStringLiteral("SELECT id FROM chunks WHERE text LIKE ? ESCAPE '\\'");
    sql += collectionFilter;
    sql += QStringLiteral(" ORDER BY id ASC");
    if (limit > 0) {
        sql += QStringLiteral(" LIMIT ?");
    }
    like.prepare(sql);
    like.addBindValue(QLatin1Char('%') + escaped + QLatin1Char('%'));
    for (qint64 id : collectionIds) {
        like.addBindValue(id);
    }
    if (limit > 0) {
        like.addBindValue(limit);
    }
    if (!like.exec()) {
        setError(error, QStringLiteral("关键词检索失败：%1").arg(like.lastError().text()));
        return result;
    }
    while (like.next()) {
        result.append(like.value(0).toLongLong());
    }
    return result;
}

// ---------------------------------------------------------------------- 来源

bool LibraryStore::appendCitation(const CitationRecord &record, qint64 *newId, QString *error) const
{
    if (newId) {
        *newId = 0;
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法写入引用来源"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "INSERT INTO citations (message_id, conversation_id, chunk_id, file_id, collection_id,"
        " loc_label, rank, score, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(record.messageId);
    query.addBindValue(record.conversationId);
    query.addBindValue(record.chunkId);
    query.addBindValue(record.fileId);
    query.addBindValue(record.collectionId);
    query.addBindValue(nonNullText(record.locLabel));
    query.addBindValue(record.rank);
    query.addBindValue(record.score);
    query.addBindValue(record.createdAt > 0 ? record.createdAt : nowMillis());
    if (!query.exec()) {
        setError(error, QStringLiteral("写入引用来源失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (newId) {
        *newId = query.lastInsertId().toLongLong();
    }
    return true;
}

QList<CitationRecord> LibraryStore::citationsOfMessage(qint64 messageId, QString *error) const
{
    QList<CitationRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取引用来源"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT %1 FROM citations WHERE message_id = ?"
                                 " ORDER BY rank ASC, id ASC")
                      .arg(kCitationColumns));
    query.addBindValue(messageId);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取引用来源失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readCitation(query));
    }
    return result;
}

bool LibraryStore::removeCitationsOfMessage(qint64 messageId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法清理引用来源"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM citations WHERE message_id = ?"));
    query.addBindValue(messageId);
    if (!query.exec()) {
        setError(error, QStringLiteral("清理引用来源失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool LibraryStore::removeCitationsOfConversation(qint64 conversationId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法清理引用来源"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM citations WHERE conversation_id = ?"));
    query.addBindValue(conversationId);
    if (!query.exec()) {
        setError(error, QStringLiteral("清理引用来源失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}
