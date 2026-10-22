#include "core/ai/ProviderCatalog.h"
#include "core/chat/ChatController.h"
#include "core/chat/ChatHistoryModel.h"
#include "platform/PlatformAdapter.h"
#include "storage/AiConfigStore.h"
#include "storage/ChatStore.h"
#include "storage/Database.h"
#include "storage/SecureStore.h"
#include "fakes/FakeNetworkTransport.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#ifndef MEOWAI_SOURCE_DIR
#define MEOWAI_SOURCE_DIR "."
#endif

namespace {

/// 明文可逆的平台替身：只用于让 SecureStore 在离线测试中可用
class FakePlatform : public PlatformAdapter
{
public:
    QString platformId() const override { return QStringLiteral("test"); }
    QString appDataRoot() const override { return QDir::tempPath(); }
    QString cacheRoot() const override { return QDir::tempPath(); }
    QString bundledSkillsRoot() const override { return QString(); }
    bool supportsCustomTitleBar() const override { return false; }
    bool isNetworkAvailable() const override { return true; }
    bool supportsSecretEncryption() const override { return true; }
    bool protectSecret(const QByteArray &plain, QByteArray *blob, QString *error) override
    {
        Q_UNUSED(error)
        *blob = plain;
        return true;
    }
    bool unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error) override
    {
        Q_UNUSED(error)
        *plain = blob;
        return true;
    }
    bool supportsSelfUpdate() const override { return false; }
    bool installUpdate(const QString &, QString *error) override
    {
        if (error)
            *error = QStringLiteral("不支持自更新");
        return false;
    }
};

QString catalogPath()
{
    return QStringLiteral(MEOWAI_SOURCE_DIR) + QStringLiteral("/resources/providers.json");
}

/// 标准 SSE 分片：正文 + 带 usage 的收尾帧
QByteArray sseChunk(const QString &content)
{
    QJsonObject delta;
    delta.insert(QStringLiteral("content"), content);
    QJsonObject choice;
    choice.insert(QStringLiteral("delta"), delta);
    choice.insert(QStringLiteral("finish_reason"), QJsonValue::Null);
    QJsonArray choices;
    choices.append(choice);
    QJsonObject root;
    root.insert(QStringLiteral("choices"), choices);
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact)
        + QByteArrayLiteral("\n\n");
}

QByteArray sseFinal(int prompt, int completion, int total)
{
    QJsonObject delta;
    QJsonObject choice;
    choice.insert(QStringLiteral("delta"), delta);
    choice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));
    QJsonArray choices;
    choices.append(choice);
    QJsonObject usage;
    usage.insert(QStringLiteral("prompt_tokens"), prompt);
    usage.insert(QStringLiteral("completion_tokens"), completion);
    usage.insert(QStringLiteral("total_tokens"), total);
    QJsonObject root;
    root.insert(QStringLiteral("choices"), choices);
    root.insert(QStringLiteral("usage"), usage);
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact)
        + QByteArrayLiteral("\n\ndata: [DONE]\n\n");
}

}  // namespace

class TstChatFeatures : public QObject
{
    Q_OBJECT

private slots:
    void renderMarkdownSupportsCommonBlocks();
    void renderMarkdownEmptyReturnsEmpty();
    void regenerateLastDoesNotDuplicateUserMessage();
    void customProviderModelFetchUsesItsBaseUrl();
    void responseUsageAndElapsedArePersisted();

private:
    struct Harness
    {
        QTemporaryDir tmp;
        FakePlatform platform;
        SecureStore secure{&platform, tmp.path()};
        Database db;
        std::unique_ptr<ChatStore> store;
        std::unique_ptr<ChatHistoryModel> model;
        AiConfigStore ai{QDir(tmp.path()).filePath(QStringLiteral("config.json"))};
        ProviderCatalog catalog{catalogPath()};
        FakeNetworkTransport transport;
        std::unique_ptr<ChatController> chat;

        bool init(QString *error)
        {
            if (!tmp.isValid())
                return false;
            if (!db.open(QDir(tmp.path()).filePath(QStringLiteral("metadata.db")), error))
                return false;
            if (!db.ensureSchema(error))
                return false;

            store = std::make_unique<ChatStore>(&db);
            model = std::make_unique<ChatHistoryModel>(store.get());
            if (!model->available()) {
                if (error)
                    *error = QStringLiteral("会话模型不可用");
                return false;
            }
            model->reload();

            AiConfigData config;
            config.providerId = QStringLiteral("custom");
            config.modelId = QStringLiteral("test-model");
            config.customBaseUrl = QStringLiteral("https://custom.example.com/v1");
            if (!ai.save(config, error))
                return false;

            if (!secure.save(SecureStore::providerApiKeyId(QStringLiteral("custom")),
                             QStringLiteral("sk-test"), error)) {
                return false;
            }

            if (!catalog.load(error))
                return false;

            chat = std::make_unique<ChatController>(&secure, model.get(), &catalog, &ai,
                                                     nullptr, nullptr, &transport);
            chat->setModelId(QStringLiteral("test-model"));
            return true;
        }

        void finishRound(const QString &first, const QString &second)
        {
            const int handle = transport.m_lastHandle;
            transport.pushChunk(handle, sseChunk(first));
            transport.pushChunk(handle, sseChunk(second));
            transport.pushChunk(handle, sseFinal(10, 20, 30));
            transport.pushFinished(handle);
        }
    };
};

void TstChatFeatures::renderMarkdownSupportsCommonBlocks()
{
    Harness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    const QString html = harness.chat->renderMarkdown(QStringLiteral(
        "# 标题\n\n"
        "- 一\n"
        "- 二\n\n"
        "| 列 A | 列 B |\n"
        "| --- | --- |\n"
        "| 1 | 2 |\n\n"
        "```cpp\nint main() { return 0; }\n```\n\n"
        "[链接](https://example.com)\n"));

    QVERIFY(html.contains(QStringLiteral("<h1")));
    QVERIFY(html.contains(QStringLiteral("<ul")));
    QVERIFY(html.contains(QStringLiteral("<li")));
    QVERIFY(html.contains(QStringLiteral("<table")));
    // 代码块在 Qt 的 HTML 输出里可能表现为 <pre> 或等宽字体块，两者都视为可读
    QVERIFY(html.contains(QStringLiteral("<pre")) || html.contains(QStringLiteral("monospace")));
    QVERIFY(html.contains(QStringLiteral("int main()")));
    QVERIFY(html.contains(QStringLiteral("example.com")));
}

void TstChatFeatures::renderMarkdownEmptyReturnsEmpty()
{
    Harness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    QVERIFY(harness.chat->renderMarkdown(QString()).isEmpty());
    QVERIFY(harness.chat->renderMarkdown(QStringLiteral("   \n\t ")).isEmpty());
}

void TstChatFeatures::regenerateLastDoesNotDuplicateUserMessage()
{
    Harness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    harness.chat->send(QStringLiteral("hello"));
    QCOMPARE(harness.transport.m_requests.size(), 1);
    harness.finishRound(QStringLiteral("first"), QString());
    QVERIFY(!harness.chat->busy());
    QCOMPARE(harness.model->rowCount(), 2);

    QList<MessageRecord> before = harness.store->messages(harness.model->conversationId(), &error);
    QCOMPARE(before.size(), 2);
    const qint64 userId = before.first().id;

    harness.chat->regenerateLast();
    QCOMPARE(harness.transport.m_requests.size(), 2);

    // 重新生成的请求体只含一条用户消息（另有置顶的时间上下文），没有重复用户消息
    const QJsonObject body =
        QJsonDocument::fromJson(harness.transport.m_requests.last().body).object();
    const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
    int userCount = 0;
    QString userContent;
    for (const QJsonValue &value : messages) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("role")).toString() == QStringLiteral("user")) {
            ++userCount;
            userContent = entry.value(QStringLiteral("content")).toString();
        }
    }
    QCOMPARE(userCount, 1);
    QCOMPARE(userContent, QStringLiteral("hello"));

    harness.finishRound(QStringLiteral("second"), QString());
    QVERIFY(!harness.chat->busy());

    const QList<MessageRecord> after = harness.store->messages(harness.model->conversationId(), &error);
    QCOMPARE(after.size(), 2);
    QCOMPARE(after.first().role, QStringLiteral("user"));
    QCOMPARE(after.first().id, userId);  // 用户消息没有被重写
    QCOMPARE(after.last().role, QStringLiteral("assistant"));
    QCOMPARE(after.last().content, QStringLiteral("second"));
}

void TstChatFeatures::customProviderModelFetchUsesItsBaseUrl()
{
    Harness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    QVERIFY(harness.chat->modelListFetchable());
    harness.chat->fetchModelList();

    QCOMPARE(harness.transport.m_requests.size(), 1);
    QCOMPARE(harness.transport.m_requests.first().method, QStringLiteral("GET"));
    QCOMPARE(harness.transport.m_requests.first().url,
             QStringLiteral("https://custom.example.com/v1/models"));

    const int handle = harness.transport.m_lastHandle;
    harness.transport.pushChunk(
        handle, QByteArrayLiteral("{\"data\":[{\"id\":\"test-model\"},{\"id\":\"other-model\"}]}"));
    harness.transport.pushFinished(handle);

    // 成功时无错误提示；拉取到的额外模型进入下拉可选清单
    QVERIFY2(harness.chat->modelListMessage().isEmpty(),
             qPrintable(harness.chat->modelListMessage()));
    bool found = false;
    const QVariantList options = harness.chat->modelOptions(QStringLiteral("custom"));
    for (const QVariant &entry : options) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("other-model"))
            found = true;
    }
    QVERIFY(found);
}

void TstChatFeatures::responseUsageAndElapsedArePersisted()
{
    Harness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    harness.chat->send(QStringLiteral("统计"));
    const int handle = harness.transport.m_lastHandle;
    harness.transport.pushChunk(handle, sseChunk(QStringLiteral("ok")));
    QTest::qWait(20);
    harness.transport.pushChunk(handle, sseFinal(11, 22, 33));
    harness.transport.pushFinished(handle);

    const QList<MessageRecord> messages =
        harness.store->messages(harness.model->conversationId(), &error);
    QCOMPARE(messages.size(), 2);
    const MessageRecord assistant = messages.last();
    QCOMPARE(assistant.totalTokens, 33);
    QCOMPARE(assistant.promptTokens, 11);
    QCOMPARE(assistant.completionTokens, 22);
    QVERIFY(assistant.elapsedMs >= 0);

    QCOMPARE(harness.model->data(harness.model->index(1, 0),
                                 ChatHistoryModel::TotalTokensRole)
                 .toInt(),
             33);
    QCOMPARE(harness.model->data(harness.model->index(1, 0),
                                 ChatHistoryModel::ElapsedMsRole)
                 .toLongLong(),
             assistant.elapsedMs);
}

QTEST_GUILESS_MAIN(TstChatFeatures)
#include "tst_chatfeatures.moc"
