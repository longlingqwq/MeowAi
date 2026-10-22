#pragma once

#include <QList>
#include <QString>

class Database;

/**
 * 会话历史的数据记录（storage 层内部结构，不暴露给 UI）。
 */
struct ConversationRecord
{
    qint64 id = 0;
    QString title;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct MessageRecord
{
    qint64 id = 0;
    qint64 conversationId = 0;
    QString role;    ///< "user" | "assistant" | "system"
    QString content;
    ///< "complete" | "streaming" | "partial" | "error"
    QString status = QStringLiteral("complete");
    qint64 createdAt = 0;
    /// usage 与耗时：-1 表示未知（厂商未回传 / 失败 / 取消）
    int totalTokens = -1;
    int promptTokens = -1;
    int completionTokens = -1;
    qint64 elapsedMs = -1;
};

/**
 * 会话历史存储（storage 层）。
 *
 * 只做一件事：把 conversations / messages 两张表读写成结构化记录。
 * **不含任何业务判断**（标题生成、上下文裁剪、流式节流都在 core 层），
 * 也不做 UI 展示。所有失败路径返回 false + error，绝不抛异常、不崩溃。
 *
 * 复用已有 SQLite `Database`（表结构见 `Database::ensureSchema` 的 v1 迁移），
 * 不新建数据库、不新建存储机制。
 */
class ChatStore
{
public:
    explicit ChatStore(Database *database);

    /// 底层数据库是否可用；不可用时全部读写返回失败
    bool isAvailable() const;

    /// 会话列表，按 updated_at 倒序（最近使用在前）
    QList<ConversationRecord> conversations(QString *error) const;
    bool createConversation(const QString &title, qint64 *newId, QString *error) const;
    bool renameConversation(qint64 conversationId, const QString &title, QString *error) const;
    bool removeConversation(qint64 conversationId, QString *error) const;
    /// 刷新 updated_at（每轮对话结束后调用，保证会话列表排序正确）
    bool touchConversation(qint64 conversationId, QString *error) const;

    /// 某会话的消息，按 id 升序
    QList<MessageRecord> messages(qint64 conversationId, QString *error) const;
    bool appendMessage(const MessageRecord &record, qint64 *newId, QString *error) const;
    /// 更新内容与状态（流式节流落库 / 终局落库共用）
    bool updateMessage(qint64 messageId, const QString &content, const QString &status,
                       QString *error) const;
    /// 终局写入 usage 与耗时（与内容/状态解耦，避免流式节流覆盖）；-1 表示未知
    bool updateMessageUsage(qint64 messageId, int totalTokens, int promptTokens,
                            int completionTokens, qint64 elapsedMs, QString *error) const;
    /// 删除单条消息（重新生成时删除最后一条助手消息）
    bool removeMessage(qint64 messageId, QString *error) const;

    /// 启动时调用：把上次进程中断遗留的 streaming 行降级为 partial，避免永久卡在生成中
    bool demoteInterruptedMessages(QString *error) const;

private:
    Database *m_database = nullptr;
};
