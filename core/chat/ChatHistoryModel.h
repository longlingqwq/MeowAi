#pragma once

#include "providers/ChatTypes.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QTimer>
#include <QVariantList>

class ChatStore;

/**
 * 会话历史模型（core 层）—— 暴露给 QML 的**真实、可持久化**的消息流。
 *
 * 职责边界（不与其他模块杂糅）：
 *  - 行 = 当前会话的消息；角色 / 内容 / 状态 / 时间通过 roleNames 暴露；
 *  - 持久化全部委托 `storage/ChatStore`，本层只做业务编排：
 *    自动标题、上下文裁剪、流式**节流落库**（约 400ms）+ 终局落库；
 *  - 不含任何网络细节（由 ChatController / providers 负责）。
 *
 * 防崩溃：依赖为空（数据库不可用）、DB 损坏 / 锁定、流式写一半失败
 * 一律降级为「内存内可用、持久化跳过」，只写 lastError，绝不抛异常。
 */
class ChatHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 conversationId READ conversationId NOTIFY conversationChanged)
    Q_PROPERTY(QString conversationTitle READ conversationTitle NOTIFY conversationChanged)
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Roles {
        RoleRole = Qt::UserRole + 1,
        ContentRole,
        MessageIdRole,
        StatusRole,
        CreatedAtRole,
    };

    /// store 可为 nullptr（依赖未就绪时模型仍可构造，只是不可持久化）
    explicit ChatHistoryModel(ChatStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 conversationId() const;
    QString conversationTitle() const;
    QVariantList conversations() const;
    bool available() const;
    QString lastError() const;

    /// 载入会话列表并打开最近一个；无会话时自动新建一个（启动时调用）
    Q_INVOKABLE void reload();
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void openConversation(qint64 id);
    Q_INVOKABLE void renameConversation(qint64 id, const QString &title);
    Q_INVOKABLE void removeConversation(qint64 id);

    // --- 供 ChatController 写入当前会话（非 QML 接口）---
    void appendUserMessage(const QString &text);
    /// 追加一条空的 assistant 占位（status = streaming），随后由 delta 填充
    void beginAssistantMessage();
    /// 流式增量：内存立即追加，落库按 kFlushIntervalMs 节流
    void appendAssistantDelta(const QString &delta);
    /// 终局：status ∈ complete | partial | error；finalText 非空时以它为准（对齐整包响应）
    void finalizeAssistantMessage(const QString &status, const QString &finalText = QString());

    /// 有界上下文：从最新往前取，跳过空占位与失败消息，受条数与字符数双重限制
    QList<ChatMessage> requestHistory(int maxMessages, int maxChars) const;

signals:
    void conversationChanged();
    void conversationsChanged();
    void availableChanged();
    void lastErrorChanged();

private:
    struct Row
    {
        qint64 id = 0;
        QString role;
        QString content;
        QString status;
        qint64 createdAt = 0;
    };

    void setLastError(const QString &message);
    void refreshConversations();
    void clearRows();
    void loadRows(qint64 conversationId);
    void openConversationInternal(qint64 id);
    /// 把节流中的内容立即落库（无待写内容时为空操作）
    void flushPending();
    int rowIndexForId(qint64 messageId) const;

    ChatStore *m_store = nullptr;

    QList<Row> m_rows;
    QVariantList m_conversations;
    qint64 m_conversationId = 0;
    QString m_conversationTitle;
    QString m_lastError;

    /// 待落库的 assistant 消息（节流目标）
    qint64 m_pendingMessageId = 0;
    QTimer m_flushTimer;
};
