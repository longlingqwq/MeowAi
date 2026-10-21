#include "core/chat/ChatHistoryModel.h"

#include "storage/ChatStore.h"

#include <QDateTime>
#include <QVariantMap>

namespace {

constexpr int kFlushIntervalMs = 400;
constexpr int kAutoTitleLength = 24;

constexpr auto kStatusComplete = "complete";
constexpr auto kStatusStreaming = "streaming";

QString roleToString(ChatMessage::Role role)
{
    switch (role) {
    case ChatMessage::System:
        return QStringLiteral("system");
    case ChatMessage::Assistant:
        return QStringLiteral("assistant");
    case ChatMessage::User:
    default:
        return QStringLiteral("user");
    }
}

ChatMessage::Role roleFromString(const QString &role)
{
    if (role == QLatin1String("assistant")) {
        return ChatMessage::Assistant;
    }
    if (role == QLatin1String("system")) {
        return ChatMessage::System;
    }
    return ChatMessage::User;
}

/// 用首条用户消息生成会话标题（去除换行、截断）
QString deriveTitle(const QString &text)
{
    const QString flat = QString(text).simplified();
    if (flat.isEmpty()) {
        return QString();
    }
    return flat.left(kAutoTitleLength);
}

} // namespace

ChatHistoryModel::ChatHistoryModel(ChatStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    m_flushTimer.setInterval(kFlushIntervalMs);
    m_flushTimer.setSingleShot(true);
    connect(&m_flushTimer, &QTimer::timeout, this, &ChatHistoryModel::flushPending);

    // 依赖不可用时立即给出原因，UI 无需等到首次 reload 才拿到错误
    if (!available()) {
        setLastError(QStringLiteral("会话历史不可用：本地数据库未就绪"));
    }
}

int ChatHistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant ChatHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RoleRole:
        return row.role;
    case ContentRole:
        return row.content;
    case MessageIdRole:
        return row.id;
    case StatusRole:
        return row.status;
    case CreatedAtRole:
        return row.createdAt;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ChatHistoryModel::roleNames() const
{
    return {
        { RoleRole, "role" },
        { ContentRole, "content" },
        { MessageIdRole, "messageId" },
        { StatusRole, "status" },
        { CreatedAtRole, "createdAt" },
    };
}

qint64 ChatHistoryModel::conversationId() const
{
    return m_conversationId;
}

QString ChatHistoryModel::conversationTitle() const
{
    return m_conversationTitle;
}

QVariantList ChatHistoryModel::conversations() const
{
    return m_conversations;
}

bool ChatHistoryModel::available() const
{
    return m_store && m_store->isAvailable();
}

QString ChatHistoryModel::lastError() const
{
    return m_lastError;
}

void ChatHistoryModel::reload()
{
    if (!available()) {
        clearRows();
        m_conversations.clear();
        m_conversationId = 0;
        m_conversationTitle.clear();
        emit conversationsChanged();
        emit conversationChanged();
        emit availableChanged();
        setLastError(QStringLiteral("会话历史不可用：本地数据库未就绪"));
        return;
    }

    setLastError(QString());

    // 进程中断遗留的 streaming 行降级为 partial，避免永久停留在「生成中」
    QString error;
    m_store->demoteInterruptedMessages(&error);
    if (!error.isEmpty()) {
        setLastError(error);
    }

    QList<ConversationRecord> list = m_store->conversations(&error);
    if (!error.isEmpty()) {
        setLastError(error);
    }

    if (list.isEmpty()) {
        qint64 id = 0;
        QString createError;
        if (!m_store->createConversation(QString(), &id, &createError)) {
            setLastError(createError);
            emit availableChanged();
            return;
        }
        openConversationInternal(id);
    } else {
        openConversationInternal(list.first().id);
    }

    refreshConversations();
    emit availableChanged();
}

void ChatHistoryModel::newConversation()
{
    if (!available()) {
        setLastError(QStringLiteral("会话历史不可用，无法新建会话"));
        return;
    }
    qint64 id = 0;
    QString error;
    if (!m_store->createConversation(QString(), &id, &error)) {
        setLastError(error);
        return;
    }
    setLastError(QString());
    openConversationInternal(id);
    refreshConversations();
}

void ChatHistoryModel::openConversation(qint64 id)
{
    if (!available()) {
        return;
    }
    openConversationInternal(id);
}

void ChatHistoryModel::renameConversation(qint64 id, const QString &title)
{
    if (!available()) {
        return;
    }
    QString error;
    if (!m_store->renameConversation(id, title, &error)) {
        setLastError(error);
        return;
    }
    setLastError(QString());
    if (id == m_conversationId) {
        m_conversationTitle = title;
        emit conversationChanged();
    }
    refreshConversations();
}

void ChatHistoryModel::removeConversation(qint64 id)
{
    if (!available()) {
        return;
    }
    if (m_pendingMessageId != 0 && m_rows.size() > 0) {
        m_flushTimer.stop();
        flushPending();
    }

    QString error;
    if (!m_store->removeConversation(id, &error)) {
        setLastError(error);
        return;
    }
    setLastError(QString());

    if (id == m_conversationId) {
        const QList<ConversationRecord> list = m_store->conversations(&error);
        if (list.isEmpty()) {
            qint64 newId = 0;
            if (!m_store->createConversation(QString(), &newId, &error)) {
                setLastError(error);
                clearRows();
                refreshConversations();
                return;
            }
            openConversationInternal(newId);
        } else {
            openConversationInternal(list.first().id);
        }
    }
    refreshConversations();
}

void ChatHistoryModel::appendUserMessage(const QString &text)
{
    if (!available()) {
        return;
    }
    if (m_conversationId == 0) {
        loadRows(0);
        if (m_conversationId == 0) {
            setLastError(QStringLiteral("当前没有可用会话"));
            return;
        }
    }

    MessageRecord record;
    record.conversationId = m_conversationId;
    record.role = roleToString(ChatMessage::User);
    record.content = text;
    record.status = QLatin1String(kStatusComplete);
    record.createdAt = QDateTime::currentMSecsSinceEpoch();

    qint64 newId = 0;
    QString error;
    if (!m_store->appendMessage(record, &newId, &error)) {
        setLastError(error);
    }
    record.id = newId;

    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    Row row;
    row.id = newId;
    row.role = record.role;
    row.content = record.content;
    row.status = record.status;
    row.createdAt = record.createdAt;
    m_rows.append(row);
    endInsertRows();

    // 首条用户消息顺便生成会话标题
    if (m_conversationTitle.trimmed().isEmpty()) {
        const QString title = deriveTitle(text);
        if (!title.isEmpty()) {
            QString renameError;
            if (m_store->renameConversation(m_conversationId, title, &renameError)) {
                m_conversationTitle = title;
                emit conversationChanged();
                refreshConversations();
            } else {
                setLastError(renameError);
            }
        }
    }
}

qint64 ChatHistoryModel::beginAssistantMessage()
{
    if (!available() || m_conversationId == 0) {
        return 0;
    }

    MessageRecord record;
    record.conversationId = m_conversationId;
    record.role = roleToString(ChatMessage::Assistant);
    record.status = QLatin1String(kStatusStreaming);
    record.createdAt = QDateTime::currentMSecsSinceEpoch();

    qint64 newId = 0;
    QString error;
    if (!m_store->appendMessage(record, &newId, &error)) {
        setLastError(error);
    }

    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    Row row;
    row.id = newId;
    row.role = record.role;
    row.status = record.status;
    row.createdAt = record.createdAt;
    m_rows.append(row);
    endInsertRows();

    m_pendingMessageId = newId;
    return newId;
}

void ChatHistoryModel::appendAssistantDelta(const QString &delta)
{
    if (delta.isEmpty() || m_rows.isEmpty()) {
        return;
    }

    Row &row = m_rows.last();
    row.content.append(delta);
    const QModelIndex idx = index(m_rows.size() - 1, 0);
    emit dataChanged(idx, idx, { ContentRole });

    if (row.id != 0) {
        m_pendingMessageId = row.id;
        m_flushTimer.start();
    }
}

void ChatHistoryModel::finalizeAssistantMessage(const QString &status, const QString &finalText)
{
    m_flushTimer.stop();

    if (m_rows.isEmpty()) {
        m_pendingMessageId = 0;
        return;
    }

    const int lastIndex = m_rows.size() - 1;
    Row &row = m_rows.last();
    if (row.role != QLatin1String("assistant")) {
        m_pendingMessageId = 0;
        return;
    }

    if (!finalText.isEmpty()) {
        row.content = finalText;
    }
    row.status = status;

    if (available() && row.id != 0) {
        QString error;
        if (!m_store->updateMessage(row.id, row.content, row.status, &error)) {
            setLastError(error);
        }
        QString touchError;
        if (!m_store->touchConversation(m_conversationId, &touchError)) {
            setLastError(touchError);
        }
    }
    m_pendingMessageId = 0;

    const QModelIndex idx = index(lastIndex, 0);
    emit dataChanged(idx, idx, { ContentRole, StatusRole });
    refreshConversations();
}

QList<ChatMessage> ChatHistoryModel::requestHistory(int maxMessages, int maxChars) const
{
    QList<ChatMessage> result;
    if (maxMessages <= 0) {
        return result;
    }

    int chars = 0;
    for (int i = m_rows.size() - 1; i >= 0 && result.size() < maxMessages; --i) {
        const Row &row = m_rows.at(i);
        const QString content = row.content;
        if (content.trimmed().isEmpty()) {
            continue; // 跳过流式占位（尚未产生内容）
        }
        if (row.status == QLatin1String("error")) {
            continue; // 失败回复不进入上下文，避免污染后续请求
        }
        if (!result.isEmpty() && chars + content.size() > maxChars) {
            break;
        }
        ChatMessage message;
        message.role = roleFromString(row.role);
        message.text = content;
        result.prepend(message);
        chars += content.size();
    }
    return result;
}

void ChatHistoryModel::setLastError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

void ChatHistoryModel::refreshConversations()
{
    if (!available()) {
        m_conversations.clear();
        emit conversationsChanged();
        return;
    }

    QString error;
    const QList<ConversationRecord> list = m_store->conversations(&error);
    if (!error.isEmpty()) {
        setLastError(error);
    }

    QVariantList items;
    items.reserve(list.size());
    for (const ConversationRecord &record : list) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), record.id);
        item.insert(QStringLiteral("title"), record.title);
        item.insert(QStringLiteral("updatedAt"), record.updatedAt);
        items.append(item);
    }
    m_conversations = items;
    emit conversationsChanged();
}

void ChatHistoryModel::clearRows()
{
    if (m_rows.isEmpty()) {
        return;
    }
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void ChatHistoryModel::loadRows(qint64 conversationId)
{
    m_flushTimer.stop();
    m_pendingMessageId = 0;

    beginResetModel();
    m_rows.clear();
    m_conversationId = conversationId;
    m_conversationTitle.clear();

    if (available() && conversationId > 0) {
        QString error;
        const QList<ConversationRecord> list = m_store->conversations(&error);
        for (const ConversationRecord &record : list) {
            if (record.id == conversationId) {
                m_conversationTitle = record.title;
                break;
            }
        }
        const QList<MessageRecord> records = m_store->messages(conversationId, &error);
        if (!error.isEmpty()) {
            setLastError(error);
        }
        m_rows.reserve(records.size());
        for (const MessageRecord &record : records) {
            Row row;
            row.id = record.id;
            row.role = record.role;
            row.content = record.content;
            row.status = record.status;
            row.createdAt = record.createdAt;
            m_rows.append(row);
        }
    }
    endResetModel();
    emit conversationChanged();
}

void ChatHistoryModel::openConversationInternal(qint64 id)
{
    loadRows(id);
}

void ChatHistoryModel::flushPending()
{
    if (m_pendingMessageId == 0) {
        return;
    }
    const int idx = rowIndexForId(m_pendingMessageId);
    if (idx < 0 || !available()) {
        m_pendingMessageId = 0;
        return;
    }
    QString error;
    if (!m_store->updateMessage(m_pendingMessageId, m_rows.at(idx).content,
                                QLatin1String(kStatusStreaming), &error)) {
        setLastError(error);
    }
}

int ChatHistoryModel::rowIndexForId(qint64 messageId) const
{
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (m_rows.at(i).id == messageId) {
            return i;
        }
    }
    return -1;
}
