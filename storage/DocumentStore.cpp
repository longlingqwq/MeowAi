#include "storage/DocumentStore.h"

#include "storage/Database.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

/// SQLite 的 NOT NULL 列不接受 NULL，而 `QString()` 是 null 字符串、会被绑定成 NULL。
QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

DocumentRecord readDocument(const QSqlQuery &query)
{
    DocumentRecord record;
    record.id = query.value(0).toLongLong();
    record.fileId = query.value(1).toLongLong();
    record.tokenEstTotal = query.value(2).toInt();
    record.sectionSource = query.value(3).toString();
    record.sectionCount = query.value(4).toInt();
    record.builtAt = query.value(5).toLongLong();
    return record;
}

SectionRecord readSection(const QSqlQuery &query)
{
    SectionRecord record;
    record.id = query.value(0).toLongLong();
    record.documentId = query.value(1).toLongLong();
    record.ordinal = query.value(2).toInt();
    record.title = query.value(3).toString();
    record.chunkStart = query.value(4).toInt();
    record.chunkEnd = query.value(5).toInt();
    record.tokenEst = query.value(6).toInt();
    return record;
}

} // namespace

DocumentStore::DocumentStore(Database *database)
    : m_database(database)
{
}

bool DocumentStore::isAvailable() const
{
    return m_database && m_database->isOpen();
}

// ---------------------------------------------------------- documents

bool DocumentStore::hasDocument(qint64 fileId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT 1 FROM documents WHERE file_id = ? LIMIT 1"));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("检查 documents 失败：%1").arg(query.lastError().text()));
        return false;
    }
    return query.next();
}

bool DocumentStore::documentByFile(qint64 fileId, DocumentRecord *out, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "SELECT id, file_id, token_est_total, section_source, section_count, built_at"
        " FROM documents WHERE file_id = ? LIMIT 1"));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("查询 documents 失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (!query.next()) {
        return false;  // 没有对应记录不是错误
    }
    if (out) {
        *out = readDocument(query);
    }
    return true;
}

bool DocumentStore::documentById(qint64 id, DocumentRecord *out, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "SELECT id, file_id, token_est_total, section_source, section_count, built_at"
        " FROM documents WHERE id = ? LIMIT 1"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, QStringLiteral("查询 documents 失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (!query.next()) {
        return false;
    }
    if (out) {
        *out = readDocument(query);
    }
    return true;
}

bool DocumentStore::upsertDocument(const DocumentRecord &record, qint64 *newId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }
    QSqlDatabase db = m_database->handle();
    if (!db.transaction()) {
        setError(error, QStringLiteral("开启事务失败"));
        return false;
    }

    // 先删除同 fileId 的旧行（级联清理 sections），避免 UNIQUE 冲突
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM documents WHERE file_id = ?"));
    del.addBindValue(record.fileId);
    if (!del.exec()) {
        db.rollback();
        setError(error, QStringLiteral("清理旧 documents 失败：%1").arg(del.lastError().text()));
        return false;
    }

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO documents (file_id, token_est_total, section_source, section_count, built_at)"
        " VALUES (?, ?, ?, ?, ?)"));
    ins.addBindValue(record.fileId);
    ins.addBindValue(record.tokenEstTotal);
    ins.addBindValue(nonNullText(record.sectionSource));
    ins.addBindValue(record.sectionCount);
    ins.addBindValue(record.builtAt);
    if (!ins.exec()) {
        db.rollback();
        setError(error, QStringLiteral("插入 documents 失败：%1").arg(ins.lastError().text()));
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        setError(error, QStringLiteral("提交事务失败"));
        return false;
    }

    if (newId) {
        *newId = ins.lastInsertId().toLongLong();
    }
    return true;
}

bool DocumentStore::removeDocumentByFile(qint64 fileId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM documents WHERE file_id = ?"));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("删除 documents 失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------- sections

QList<SectionRecord> DocumentStore::sectionsOfDocument(qint64 documentId, QString *error) const
{
    QList<SectionRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return result;
    }
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "SELECT id, document_id, ordinal, title, chunk_start, chunk_end, token_est"
        " FROM sections WHERE document_id = ? ORDER BY ordinal ASC"));
    query.addBindValue(documentId);
    if (!query.exec()) {
        setError(error, QStringLiteral("查询 sections 失败：%1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        result.append(readSection(query));
    }
    return result;
}

bool DocumentStore::replaceSections(qint64 documentId, const QList<SectionRecord> &sections,
                                    QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }
    QSqlDatabase db = m_database->handle();
    if (!db.transaction()) {
        setError(error, QStringLiteral("开启事务失败"));
        return false;
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM sections WHERE document_id = ?"));
    del.addBindValue(documentId);
    if (!del.exec()) {
        db.rollback();
        setError(error, QStringLiteral("清理旧 sections 失败：%1").arg(del.lastError().text()));
        return false;
    }

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO sections (document_id, ordinal, title, chunk_start, chunk_end, token_est)"
        " VALUES (?, ?, ?, ?, ?, ?)"));
    for (const SectionRecord &s : sections) {
        ins.addBindValue(documentId);
        ins.addBindValue(s.ordinal);
        ins.addBindValue(nonNullText(s.title));
        ins.addBindValue(s.chunkStart);
        ins.addBindValue(s.chunkEnd);
        ins.addBindValue(s.tokenEst);
        if (!ins.exec()) {
            db.rollback();
            setError(error, QStringLiteral("插入 sections 失败：%1").arg(ins.lastError().text()));
            return false;
        }
    }

    if (!db.commit()) {
        db.rollback();
        setError(error, QStringLiteral("提交事务失败"));
        return false;
    }
    return true;
}
