#include "core/library/ImageCaptionService.h"
#include "core/retriever/EmbeddingService.h"
#include "providers/AiProvider.h"
#include "fakes/FakeNetworkTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

namespace {

ProviderConfig embeddingProvider()
{
    ProviderConfig config;
    config.providerId = QStringLiteral("custom");
    config.apiKey = QStringLiteral("sk-test");
    config.baseUrl = QStringLiteral("https://embed.example.com/v1");
    config.authStyle = QStringLiteral("bearer");
    return config;
}

LibraryEmbeddingConfig embeddingConfig(int dimensions)
{
    LibraryEmbeddingConfig config;
    config.providerId = QStringLiteral("custom");
    config.modelId = QStringLiteral("text-embedding-test");
    config.dimensions = dimensions;
    config.batchSize = 1;
    return config;
}

QByteArray embeddingBody(int dimensions = 4)
{
    QJsonArray values;
    for (int i = 0; i < dimensions; ++i)
        values.append(0.1 * (i + 1));

    QJsonObject item;
    item.insert(QStringLiteral("embedding"), values);
    QJsonArray data;
    data.append(item);

    QJsonObject root;
    root.insert(QStringLiteral("data"), data);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool hasHeader(const NetworkTransport::Request &request, const QString &name,
               const QString &value)
{
    for (const auto &header : request.headers) {
        if (header.first.compare(name, Qt::CaseInsensitive) == 0 && header.second == value)
            return true;
    }
    return false;
}

}  // namespace

/// 假视觉 Provider：不发网络，按脚本同步回信号；记录请求形态
class FakeCaptionProvider : public AiProvider
{
    Q_OBJECT

public:
    explicit FakeCaptionProvider(QObject *parent = nullptr) : AiProvider(parent) {}

    Protocol protocol() const override { return Protocol::OpenAI; }
    QString providerId() const override { return QStringLiteral("fake"); }
    bool isConfigured() const override { return m_configured; }
    void configure(const ProviderConfig &config) override
    {
        m_configured = !config.apiKey.isEmpty();
    }

    void sendChat(const ChatRequest &request) override { m_requests.append(request); }
    void cancel() override {}
    void validateCredentials() override {}
    void fetchModels() override {}
    void fetchModelsForService() override
    {
        emit modelsFetched(QStringList{QStringLiteral("fake-vision-1")}, QString());
    }

    void reply(const QString &text)
    {
        ChatResponse response;
        response.ok = true;
        response.text = text;
        response.model = QStringLiteral("fake-vision");
        emit chatFinished(response);
    }

    void failWith(const ChatError &error) { emit chatFailed(error); }

    QList<ChatRequest> requests() const { return m_requests; }

private:
    QList<ChatRequest> m_requests;
    bool m_configured = true;
};

class TstLibraryServices : public QObject
{
    Q_OBJECT

private slots:
    // ---- Embedding 校验 ----
    void embeddingValidationSucceeds();
    void embeddingValidationReports401();
    void embeddingValidationReports404();
    void embeddingValidationTreats429AsUnverifiable();
    void embeddingValidationTreatsTimeoutAsUnverifiable();
    void embeddingValidationReportsDimensionMismatch();
    void embeddingValidationIgnoresDuplicateCalls();

    // ---- Embedding 模型拉取 ----
    void embeddingModelFetchSucceeds();
    void embeddingModelFetchReportsMissingEndpoint();
    void embeddingModelFetchReportsParseFailure();

    // ---- Caption 校验 ----
    void captionValidationUsesTinyPngAndBypassesConsent();
    void captionValidationReports401();
    void captionValidationRejectsEmptyText();
    void captionValidationTreats429AsUnverifiable();
    void captionValidationIgnoresDuplicateCalls();

    // ---- 服务侧模型拉取 ----
    void captionModelFetchUsesServiceEntry();
};

void TstLibraryServices::embeddingValidationSucceeds()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));

    QSignalSpy spy(&service, &EmbeddingService::validationChanged);
    service.validateConfig();

    QCOMPARE(transport.m_requests.size(), 1);
    QCOMPARE(transport.m_requests.first().method, QStringLiteral("POST"));
    QCOMPARE(transport.m_requests.first().url,
             QStringLiteral("https://embed.example.com/v1/embeddings"));
    QVERIFY(hasHeader(transport.m_requests.first(), QStringLiteral("Authorization"),
                      QStringLiteral("Bearer sk-test")));

    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle, embeddingBody(4));
    transport.pushFinished(handle);

    QCOMPARE(service.validationStatus(), QStringLiteral("valid"));
    QVERIFY(service.validationMessage().contains(QStringLiteral("校验通过")));
    QVERIFY(spy.count() >= 2);  // checking + valid
}

void TstLibraryServices::embeddingValidationReports401()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));
    service.validateConfig();

    transport.pushFailed(transport.m_lastHandle, 401, NetworkTransport::Failure::Other,
                         QStringLiteral("unauthorized"));

    QCOMPARE(service.validationStatus(), QStringLiteral("invalid"));
    QCOMPARE(service.validationMessage(), QStringLiteral("密钥无效或无权限"));
}

void TstLibraryServices::embeddingValidationReports404()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));
    service.validateConfig();

    transport.pushFailed(transport.m_lastHandle, 404, NetworkTransport::Failure::Other,
                         QStringLiteral("not found"));

    QCOMPARE(service.validationStatus(), QStringLiteral("invalid"));
    QCOMPARE(service.validationMessage(), QStringLiteral("模型不存在或无此接口"));
}

void TstLibraryServices::embeddingValidationTreats429AsUnverifiable()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));
    service.validateConfig();

    transport.pushFailed(transport.m_lastHandle, 429, NetworkTransport::Failure::Other,
                         QStringLiteral("rate limited"));

    QCOMPARE(service.validationStatus(), QStringLiteral("unverifiable"));
    QCOMPARE(service.validationMessage(), QStringLiteral("触发限流，请稍后重试"));
}

void TstLibraryServices::embeddingValidationTreatsTimeoutAsUnverifiable()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));
    service.validateConfig();

    transport.pushFailed(transport.m_lastHandle, 0, NetworkTransport::Failure::Timeout,
                         QStringLiteral("timeout"));

    QCOMPARE(service.validationStatus(), QStringLiteral("unverifiable"));
    QCOMPARE(service.validationMessage(), QStringLiteral("无法判定，请检查网络或稍后重试"));
}

void TstLibraryServices::embeddingValidationReportsDimensionMismatch()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(8));
    service.validateConfig();

    transport.pushChunk(transport.m_lastHandle, embeddingBody(4));
    transport.pushFinished(transport.m_lastHandle);

    QCOMPARE(service.validationStatus(), QStringLiteral("invalid"));
    QCOMPARE(service.validationMessage(), QStringLiteral("返回维度与配置不一致"));
}

void TstLibraryServices::embeddingValidationIgnoresDuplicateCalls()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));

    service.validateConfig();
    service.validateConfig();  // checking 期间必须忽略
    service.validateConfig();

    QCOMPARE(transport.m_requests.size(), 1);
}

void TstLibraryServices::embeddingModelFetchSucceeds()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));

    QSignalSpy spy(&service, &EmbeddingService::modelsFetched);
    service.fetchModels();

    QCOMPARE(transport.m_requests.size(), 1);
    QCOMPARE(transport.m_requests.first().method, QStringLiteral("GET"));
    QCOMPARE(transport.m_requests.first().url,
             QStringLiteral("https://embed.example.com/v1/models"));

    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle,
                        QByteArrayLiteral("{\"data\":[{\"id\":\"m1\"},{\"id\":\"m2\"},{\"id\":\"m1\"}]}"));
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toStringList(), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    QVERIFY(args.at(1).toString().isEmpty());
}

void TstLibraryServices::embeddingModelFetchReportsMissingEndpoint()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));

    QSignalSpy spy(&service, &EmbeddingService::modelsFetched);
    service.fetchModels();
    transport.pushFailed(transport.m_lastHandle, 404, NetworkTransport::Failure::Other,
                         QStringLiteral("not found"));

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QVERIFY(args.at(0).toStringList().isEmpty());
    QCOMPARE(args.at(1).toString(), QStringLiteral("该服务未提供 /models 接口"));
}

void TstLibraryServices::embeddingModelFetchReportsParseFailure()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(embeddingProvider(), embeddingConfig(4));

    QSignalSpy spy(&service, &EmbeddingService::modelsFetched);
    service.fetchModels();
    transport.pushChunk(transport.m_lastHandle, QByteArrayLiteral("{\"data\":[]}"));
    transport.pushFinished(transport.m_lastHandle);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QVERIFY(args.at(0).toStringList().isEmpty());
    QCOMPARE(args.at(1).toString(), QStringLiteral("未能从厂商接口解析出模型清单"));
}

void TstLibraryServices::captionValidationUsesTinyPngAndBypassesConsent()
{
    FakeCaptionProvider provider;
    ImageCaptionService service(&provider);

    LibraryCaptionConfig config;
    config.providerId = QStringLiteral("fake");
    config.modelId = QStringLiteral("fake-vision");
    config.maxTokens = 500;
    service.setCaptionConfig(config);
    service.setConsentGiven(false);  // 队列需同意，但显式校验可绕过

    service.validateConfig();

    QCOMPARE(provider.requests().size(), 1);
    const ChatRequest request = provider.requests().first();
    QVERIFY(request.imagesConfirmed);
    QVERIFY(!request.stream);
    QCOMPARE(request.maxTokens, 16);
    QCOMPARE(request.messages.size(), 1);
    QCOMPARE(request.messages.first().images.size(), 1);
    const ChatImage image = request.messages.first().images.first();
    QCOMPARE(image.mimeType, QStringLiteral("image/png"));
    QVERIFY(!image.bytes.isEmpty());
    QVERIFY(!request.messages.first().text.isEmpty());

    provider.reply(QStringLiteral("OK"));
    QCOMPARE(service.validationStatus(), QStringLiteral("valid"));
}

void TstLibraryServices::captionValidationReports401()
{
    FakeCaptionProvider provider;
    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.providerId = QStringLiteral("fake");
    config.modelId = QStringLiteral("fake-vision");
    service.setCaptionConfig(config);

    service.validateConfig();

    ChatError error;
    error.code = ChatError::Unauthorized;
    error.httpStatus = 401;
    provider.failWith(error);

    QCOMPARE(service.validationStatus(), QStringLiteral("invalid"));
    QCOMPARE(service.validationMessage(), QStringLiteral("密钥无效或无权限"));
}

void TstLibraryServices::captionValidationRejectsEmptyText()
{
    FakeCaptionProvider provider;
    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.providerId = QStringLiteral("fake");
    config.modelId = QStringLiteral("fake-vision");
    service.setCaptionConfig(config);

    service.validateConfig();
    provider.reply(QStringLiteral("   "));

    QCOMPARE(service.validationStatus(), QStringLiteral("invalid"));
    QCOMPARE(service.validationMessage(), QStringLiteral("返回内容为空，无法确认模型可用"));
}

void TstLibraryServices::captionValidationTreats429AsUnverifiable()
{
    FakeCaptionProvider provider;
    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.providerId = QStringLiteral("fake");
    config.modelId = QStringLiteral("fake-vision");
    service.setCaptionConfig(config);

    service.validateConfig();

    ChatError error;
    error.code = ChatError::RateLimited;
    error.httpStatus = 429;
    provider.failWith(error);

    QCOMPARE(service.validationStatus(), QStringLiteral("unverifiable"));
    QCOMPARE(service.validationMessage(), QStringLiteral("触发限流，请稍后重试"));
}

void TstLibraryServices::captionValidationIgnoresDuplicateCalls()
{
    FakeCaptionProvider provider;
    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.providerId = QStringLiteral("fake");
    config.modelId = QStringLiteral("fake-vision");
    service.setCaptionConfig(config);

    service.validateConfig();
    service.validateConfig();
    service.validateConfig();

    QCOMPARE(provider.requests().size(), 1);
}

void TstLibraryServices::captionModelFetchUsesServiceEntry()
{
    FakeCaptionProvider provider;
    QSignalSpy spy(&provider, &AiProvider::modelsFetched);
    provider.fetchModelsForService();

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toStringList(), QStringList{QStringLiteral("fake-vision-1")});
    QVERIFY(args.at(1).toString().isEmpty());
}

QTEST_GUILESS_MAIN(TstLibraryServices)
#include "tst_libraryservices.moc"
