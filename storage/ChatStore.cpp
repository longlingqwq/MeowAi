#include "storage/ChatStore.h"

#include "storage/Database.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

/// 毫秒级时间戳：秒级精度会让「同一秒内创建的多个会话」失去排序区分度
qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// SQLite 的 NOT NULL 列不接受 NULL，而 `QString()` 是 null 字符串、会被绑定成 NULL。
/// 统一把 null 规范为空串，避免「空标题 / 空内容」触发约束失败。
QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

} // namespace

ChatStore::ChatStore(Database *database)
    : m_database(database)
{
}

bool ChatStore::isAvailable() const
{
    return m_database && m_database->isOpen();
}

QList<ConversationRecord> ChatStore::conversations(QString *error) const
{
    QList<ConversationRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取会话列表"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    if (!query.exec(QStringLiteral("SELECT id, title, created_at, updated_at FROM conversations"
                                   " ORDER BY updated_at DESC, id DESC"))) {
        setError(error, QStringLiteral("读取会话列表失败：%1").arg(query.lastError().text()));
        return result;
    }

    while (query.next()) {
        ConversationRecord record;
        record.id = query.value(0).toLongLong();
        record.title = query.value(1).toString();
        record.createdAt = query.value(2).toLongLong();
        record.updatedAt = query.value(3).toLongLong();
        result.append(record);
    }
    return result;
}

bool ChatStore::createConversation(const QString &title, qint64 *newId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法新建会话"));
        return false;
    }

    const qint64 now = nowMillis();
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("INSERT INTO conversations (title, created_at, updated_at)"
                                 " VALUES (?, ?, ?)"));
    query.addBindValue(nonNullText(title));
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        setError(error, QStringLiteral("新建会话失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (newId) {
        *newId = query.lastInsertId().toLongLong();
    }
    return true;
}

bool ChatStore::renameConversation(qint64 conversationId, const QString &title, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法重命名会话"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("UPDATE conversations SET title = ? WHERE id = ?"));
    query.addBindValue(nonNullText(title));
    query.addBindValue(conversationId);
    if (!query.exec()) {
        setError(error, QStringLiteral("重命名会话失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ChatStore::removeConversation(qint64 conversationId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法删除会话"));
        return false;
    }

    // messages 通过外键 ON DELETE CASCADE 一并清理（依赖连接级 PRAGMA foreign_keys = ON）
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM conversations WHERE id = ?"));
    query.addBindValue(conversationId);
    if (!query.exec()) {
        setError(error, QStringLiteral("删除会话失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ChatStore::touchConversation(qint64 conversationId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法更新会话时间"));
        return false;
    }

    // 取「全表最大 updated_at + 1」与当前毫秒的较大者：即使多个会话在同一毫秒被触碰，
    // 也保证被触碰者严格大于其余所有会话，从而一定排到会话列表最前
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "UPDATE conversations SET updated_at = MAX((SELECT COALESCE(MAX(updated_at), 0)"
        " FROM conversations) + 1, ?) WHERE id = ?"));
    query.addBindValue(nowMillis());
    query.addBindValue(conversationId);
    if (!query.exec()) {
        setError(error, QStringLiteral("更新会话时间失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<MessageRecord> ChatStore::messages(qint64 conversationId, QString *error) const
{
    QList<MessageRecord> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取消息"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT id, conversation_id, role, content, status, created_at"
                                 " FROM messages WHERE conversation_id = ? ORDER BY id ASC"));
    query.addBindValue(conversationId);
    if (!query.exec()) {
        setError(error, QStringLiteral("读取消息失败：%1").arg(query.lastError().text()));
        return result;
    }

    while (query.next()) {
        MessageRecord record;
        record.id = query.value(0).toLongLong();
        record.conversationId = query.value(1).toLongLong();
        record.role = query.value(2).toString();
        record.content = query.value(3).toString();
        record.status = query.value(4).toString();
        record.createdAt = query.value(5).toLongLong();
        result.append(record);
    }
    return result;
}

bool ChatStore::appendMessage(const MessageRecord &record, qint64 *newId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法写入消息"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "INSERT INTO messages (conversation_id, role, content, status, created_at)"
        " VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(record.conversationId);
    query.addBindValue(nonNullText(record.role));
    query.addBindValue(nonNullText(record.content));
    query.addBindValue(nonNullText(record.status));
    query.addBindValue(record.createdAt > 0 ? record.createdAt : nowMillis());
    if (!query.exec()) {
        setError(error, QStringLiteral("写入消息失败：%1").arg(query.lastError().text()));
        return false;
    }
    if (newId) {
        *newId = query.lastInsertId().toLongLong();
    }
    return true;
}

bool ChatStore::updateMessage(qint64 messageId, const QString &content, const QString &status,
                              QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法更新消息"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("UPDATE messages SET content = ?, status = ? WHERE id = ?"));
    query.addBindValue(nonNullText(content));
    query.addBindValue(nonNullText(status));
    query.addBindValue(messageId);
    if (!query.exec()) {
        setError(error, QStringLiteral("更新消息失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ChatStore::demoteInterruptedMessages(QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    if (!query.exec(QStringLiteral("UPDATE messages SET status = 'partial'"
                                   " WHERE status = 'streaming'"))) {
        setError(error, QStringLiteral("修复中断消息失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}
