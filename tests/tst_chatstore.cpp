#include "core/chat/ChatHistoryModel.h"
#include "storage/ChatStore.h"
#include "storage/Database.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 会话历史（阶段三）边界测试：
 *  - 表结构幂等迁移、CRUD 正确性；
 *  - 模型流式写入 / 节流落库 / 终局落库；
 *  - 重启后历史仍在（同库二次打开）；
 *  - 中断遗留 streaming 降级为 partial；
 *  - 有界上下文裁剪；
 *  - 数据库不可用时降级为内存态，不崩溃。
 */
class TstChatStore : public QObject
{
    Q_OBJECT

private slots:
    void schemaCreatesConversationTables();
    void conversationCrudOrdersByUpdatedAt();
    void messageLifecyclePersistsAcrossReopen();
    void modelStreamingIsThrottledThenCommitted();
    void interruptedStreamingDemotedToPartial();
    void requestHistoryIsBounded();
    void usageAndElapsedPersistAndReload();
    void legacyV3DatabaseUpgradesInPlace();
    void degradedModeWithoutDatabase();

private:
    static QString freshDbPath(const QTemporaryDir &tmp);
};

QString TstChatStore::freshDbPath(const QTemporaryDir &tmp)
{
    return QDir(tmp.path()).filePath(QStringLiteral("metadata.db"));
}

void TstChatStore::schemaCreatesConversationTables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY2(db.open(freshDbPath(tmp), &error), qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));

    const QStringList tables = db.handle().tables();
    QVERIFY(tables.contains(QStringLiteral("conversations")));
    QVERIFY(tables.contains(QStringLiteral("messages")));
    QVERIFY(tables.contains(QStringLiteral("message_attachments")));
    QVERIFY(tables.contains(QStringLiteral("meta")));

    // 幂等：重复调用不报错、不重建
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));
}

void TstChatStore::conversationCrudOrdersByUpdatedAt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY(db.open(freshDbPath(tmp), &error));
    QVERIFY(db.ensureSchema(&error));

    ChatStore store(&db);
    QVERIFY(store.isAvailable());

    qint64 first = 0;
    qint64 second = 0;
    QVERIFY(store.createConversation(QStringLiteral("第一个"), &first, &error));
    QVERIFY(store.createConversation(QStringLiteral("第二个"), &second, &error));
    QVERIFY(first > 0 && second > 0 && first != second);

    QList<ConversationRecord> list = store.conversations(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(list.size(), 2);
    // 同一秒创建时按 id 倒序，最近的在最前
    QCOMPARE(list.first().id, second);

    // 触碰后 first 应排到最前
    QVERIFY(store.touchConversation(first, &error));
    list = store.conversations(&error);
    QCOMPARE(list.first().id, first);

    QVERIFY(store.renameConversation(first, QStringLiteral("改名"), &error));
    list = store.conversations(&error);
    QCOMPARE(list.first().title, QStringLiteral("改名"));

    QVERIFY(store.removeConversation(second, &error));
    QCOMPARE(store.conversations(&error).size(), 1);
}

void TstChatStore::messageLifecyclePersistsAcrossReopen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = freshDbPath(tmp);

    qint64 conversationId = 0;
    qint64 userId = 0;
    qint64 assistantId = 0;
    {
        Database db;
        QString error;
        QVERIFY(db.open(path, &error));
        QVERIFY(db.ensureSchema(&error));

        ChatStore store(&db);
        QVERIFY(store.createConversation(QString(), &conversationId, &error));

        MessageRecord user;
        user.conversationId = conversationId;
        user.role = QStringLiteral("user");
        user.content = QStringLiteral("你好");
        QVERIFY(store.appendMessage(user, &userId, &error));

        MessageRecord assistant;
        assistant.conversationId = conversationId;
        assistant.role = QStringLiteral("assistant");
        assistant.status = QStringLiteral("streaming");
        QVERIFY(store.appendMessage(assistant, &assistantId, &error));
        QVERIFY(store.updateMessage(assistantId, QStringLiteral("你好呀"), QStringLiteral("complete"),
                                    &error));
    }

    // 重新打开同一数据库：内容与状态必须完整保留
    Database reopened;
    QString error;
    QVERIFY(reopened.open(path, &error));
    QVERIFY(reopened.ensureSchema(&error));
    ChatStore store(&reopened);

    const QList<MessageRecord> messages = store.messages(conversationId, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).role, QStringLiteral("user"));
    QCOMPARE(messages.at(0).content, QStringLiteral("你好"));
    QCOMPARE(messages.at(1).status, QStringLiteral("complete"));
    QCOMPARE(messages.at(1).content, QStringLiteral("你好呀"));
}

void TstChatStore::modelStreamingIsThrottledThenCommitted()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY(db.open(freshDbPath(tmp), &error));
    QVERIFY(db.ensureSchema(&error));

    ChatStore store(&db);
    ChatHistoryModel model(&store);
    QVERIFY(model.available());

    model.reload();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.conversationId() > 0);

    model.appendUserMessage(QStringLiteral("帮我看一下这段代码"));
    QCOMPARE(model.rowCount(), 1);
    // 首条用户消息自动生成标题
    QVERIFY(!model.conversationTitle().isEmpty());

    model.beginAssistantMessage();
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1, 0), ChatHistoryModel::StatusRole).toString(),
             QStringLiteral("streaming"));

    model.appendAssistantDelta(QStringLiteral("好的"));
    model.appendAssistantDelta(QStringLiteral("，我来看看。"));
    QCOMPARE(model.data(model.index(1, 0), ChatHistoryModel::ContentRole).toString(),
             QStringLiteral("好的，我来看看。"));

    // 节流：增量已进内存但尚未落库
    QList<MessageRecord> persisted = store.messages(model.conversationId(), &error);
    QCOMPARE(persisted.size(), 2);
    QCOMPARE(persisted.last().content, QString());

    // 节流窗口到期后落库
    QTest::qWait(600);
    persisted = store.messages(model.conversationId(), &error);
    QCOMPARE(persisted.last().content, QStringLiteral("好的，我来看看。"));

    model.finalizeAssistantMessage(QStringLiteral("complete"), QStringLiteral("好的，我来看看。"));
    persisted = store.messages(model.conversationId(), &error);
    QCOMPARE(persisted.last().status, QStringLiteral("complete"));

    // 重开模型（等价重启）：历史仍在
    ChatHistoryModel reloaded(&store);
    reloaded.reload();
    QCOMPARE(reloaded.conversationId(), model.conversationId());
    QCOMPARE(reloaded.rowCount(), 2);
    QCOMPARE(reloaded.data(reloaded.index(1, 0), ChatHistoryModel::ContentRole).toString(),
             QStringLiteral("好的，我来看看。"));
}

void TstChatStore::interruptedStreamingDemotedToPartial()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY(db.open(freshDbPath(tmp), &error));
    QVERIFY(db.ensureSchema(&error));

    ChatStore store(&db);
    ChatHistoryModel model(&store);
    model.reload();

    model.appendUserMessage(QStringLiteral("问题"));
    model.beginAssistantMessage();
    model.appendAssistantDelta(QStringLiteral("被中断的半句"));
    // 不调用 finalize，模拟进程被杀：仅有节流落库留下的 streaming 行
    QTest::qWait(600);

    const qint64 conversationId = model.conversationId();
    QList<MessageRecord> persisted = store.messages(conversationId, &error);
    QCOMPARE(persisted.last().status, QStringLiteral("streaming"));

    // 下一次启动：reload 应把遗留 streaming 降级为 partial（内容保留）
    ChatHistoryModel restarted(&store);
    restarted.reload();
    const QList<MessageRecord> repaired = store.messages(conversationId, &error);
    QCOMPARE(repaired.last().status, QStringLiteral("partial"));
    QCOMPARE(repaired.last().content, QStringLiteral("被中断的半句"));
}

void TstChatStore::requestHistoryIsBounded()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY(db.open(freshDbPath(tmp), &error));
    QVERIFY(db.ensureSchema(&error));

    ChatStore store(&db);
    ChatHistoryModel model(&store);
    model.reload();

    const QString chunk(50, QChar('x'));
    for (int i = 0; i < 6; ++i) {
        model.appendUserMessage(chunk);
    }
    QCOMPARE(model.rowCount(), 6);

    // 条数上限
    QCOMPARE(model.requestHistory(2, 100000).size(), 2);
    // 字符上限：50 字符/条 + 120 上限 → 最多 2 条
    QCOMPARE(model.requestHistory(100, 120).size(), 2);
    // 最近的在最后一条
    QVERIFY(model.requestHistory(100, 100000).last().role == ChatMessage::User);

    // 空占位（流式但无内容）不进入上下文
    model.beginAssistantMessage();
    const int beforePlaceholder = model.requestHistory(100, 100000).size();
    QCOMPARE(beforePlaceholder, 6);
}

void TstChatStore::usageAndElapsedPersistAndReload()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY(db.open(freshDbPath(tmp), &error));
    QVERIFY(db.ensureSchema(&error));

    ChatStore store(&db);
    ChatHistoryModel model(&store);
    model.reload();

    model.appendUserMessage(QStringLiteral("统计一下"));
    model.beginAssistantMessage();

    ChatUsage usage;
    usage.promptTokens = 11;
    usage.completionTokens = 22;
    usage.totalTokens = 33;
    model.finalizeAssistantMessage(QStringLiteral("complete"), QStringLiteral("完成"),
                                   usage, 1234);

    QCOMPARE(model.data(model.index(1, 0), ChatHistoryModel::TotalTokensRole).toInt(), 33);
    QCOMPARE(model.data(model.index(1, 0), ChatHistoryModel::PromptTokensRole).toInt(), 11);
    QCOMPARE(model.data(model.index(1, 0), ChatHistoryModel::CompletionTokensRole).toInt(), 22);
    QCOMPARE(model.data(model.index(1, 0), ChatHistoryModel::ElapsedMsRole).toLongLong(), 1234);

    QList<MessageRecord> persisted = store.messages(model.conversationId(), &error);
    QCOMPARE(persisted.size(), 2);
    QCOMPARE(persisted.last().totalTokens, 33);
    QCOMPARE(persisted.last().promptTokens, 11);
    QCOMPARE(persisted.last().completionTokens, 22);
    QCOMPARE(persisted.last().elapsedMs, 1234);

    // 无 usage 时存 -1（失败 / 取消 / 厂商未回传）
    model.appendUserMessage(QStringLiteral("再问一次"));
    model.beginAssistantMessage();
    model.finalizeAssistantMessage(QStringLiteral("error"));
    persisted = store.messages(model.conversationId(), &error);
    QCOMPARE(persisted.last().totalTokens, -1);
    QCOMPARE(persisted.last().promptTokens, -1);
    QCOMPARE(persisted.last().completionTokens, -1);
    QCOMPARE(persisted.last().elapsedMs, -1);

    // 重开模型：新角色可读
    ChatHistoryModel reloaded(&store);
    reloaded.reload();
    QCOMPARE(reloaded.data(reloaded.index(1, 0), ChatHistoryModel::TotalTokensRole).toInt(), 33);
    QCOMPARE(reloaded.data(reloaded.index(1, 0), ChatHistoryModel::ElapsedMsRole).toLongLong(),
             1234);
}

void TstChatStore::legacyV3DatabaseUpgradesInPlace()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = freshDbPath(tmp);
    const QString connection = QStringLiteral("legacy-v3");

    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL DEFAULT '')")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE conversations (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " title TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL DEFAULT 0,"
            " updated_at INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " conversation_id INTEGER NOT NULL, role TEXT NOT NULL,"
            " content TEXT NOT NULL DEFAULT '', status TEXT NOT NULL DEFAULT 'complete',"
            " created_at INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO meta (key, value) VALUES ('schema_version', '3')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO conversations (id, title, created_at, updated_at)"
            " VALUES (1, '旧会话', 1000, 1000)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO messages (conversation_id, role, content, status, created_at)"
            " VALUES (1, 'user', '旧消息', 'complete', 1000)")));
        legacy.close();
    }
    QSqlDatabase::removeDatabase(connection);

    Database db;
    QString error;
    QVERIFY2(db.open(path, &error), qPrintable(error));
    // 旧库自动补列；失败不崩溃
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));  // 幂等

    ChatStore store(&db);
    const QList<MessageRecord> messages = store.messages(1, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().content, QStringLiteral("旧消息"));
    QCOMPARE(messages.first().totalTokens, -1);
    QCOMPARE(messages.first().elapsedMs, -1);
}

void TstChatStore::degradedModeWithoutDatabase()
{
    // 依赖为空：模型仍可构造，全部操作安全降级
    ChatHistoryModel model(nullptr);
    QVERIFY(!model.available());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.lastError().isEmpty());

    model.reload();
    model.appendUserMessage(QStringLiteral("x"));
    model.beginAssistantMessage();
    model.appendAssistantDelta(QStringLiteral("y"));
    model.finalizeAssistantMessage(QStringLiteral("complete"));
    model.newConversation();
    QCOMPARE(model.rowCount(), 0);

    // 关闭的数据库同样降级
    Database db;
    ChatStore store(&db);
    QVERIFY(!store.isAvailable());
    QString error;
    QVERIFY(!store.createConversation(QString(), nullptr, &error));
    QVERIFY(!error.isEmpty());
    ChatHistoryModel degraded(&store);
    QVERIFY(!degraded.available());
}

QTEST_GUILESS_MAIN(TstChatStore)
#include "tst_chatstore.moc"
