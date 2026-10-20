#include "providers/AnthropicProvider.h"
#include "providers/OpenAiProvider.h"
#include "providers/ProviderFactory.h"
#include "tests/fakes/FakeNetworkTransport.h"

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <memory>

/**
 * Provider 层测试：配置校验、真实协议编解码、流式、多模态与异常流。
 * 网络通过 FakeNetworkTransport 注入，全程离线、不产生真实请求。
 */
class TstProvider : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void unconfiguredProviderIsReported();
    void configureMarksProviderReady();
    void factoryCreatesExpectedProtocols();

    void missingKeyFailsStructured();
    void streamSuccessProducesDeltasAndFinished();
    void nonStreamSuccessParsesBodyAndUsage();
    void unauthorizedIsReported();
    void rateLimitedIsReported();
    void timeoutIsReported();
    void networkUnavailableIsReported();
    void streamInterruptedIsReported();
    void emptyStreamIsReported();
    void cancelAbortsInflightRequest();
    void imagesRequireConfirmation();
    void openAiSendsImageAsDataUri();
    void anthropicSendsImageAsBase64Source();

    // v0.2.3：多厂商（baseUrl / 鉴权 / extraHeaders）、思考参数透传、Key 三态校验、清单拉取
    void customProviderUsesConfiguredBaseUrlAndBearer();
    void perVendorHeadersFollowAuthStyle();
    void anthropicAlwaysSendsApiKeyAndVersion();
    void nativeParamsAreInjectedButNotOverridingCoreFields();
    void validateReportsValidOnSuccess();
    void validateReportsInvalidOnUnauthorized();
    void validateReportsUnverifiableOnRateLimit();
    void validateReportsUnverifiableOnTimeout();
    void validateReportsUnverifiableOnNetworkFailure();
    void validateReportsNotConfiguredWithoutKey();
    void validateUsesChatMinimalWhenMethodSaysSo();
    void fetchModelsParsesVendorList();
    void fetchModelsRejectsUnsupportedVendor();
};

namespace {

ProviderConfig readyConfig()
{
    ProviderConfig config;
    config.apiKey = QStringLiteral("test-key");
    config.model = QStringLiteral("test-model");
    return config;
}

ChatRequest textRequest(const QString &text, bool stream = true)
{
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;

    ChatRequest request;
    request.model = QStringLiteral("test-model");
    request.stream = stream;
    request.messages.append(message);
    return request;
}

/// 取请求头值（大小写不敏感）；不存在返回空串
QString headerValue(const NetworkTransport::Request &request, const QString &key)
{
    for (const auto &header : request.headers) {
        if (header.first.compare(key, Qt::CaseInsensitive) == 0)
            return header.second;
    }
    return QString();
}

/// 面向多厂商的完整配置：baseUrl / authStyle / validationMethod 全给
ProviderConfig vendorConfig(const QString &baseUrl, const QString &authStyle,
                            const QString &validationMethod, const QString &providerId)
{
    ProviderConfig config;
    config.apiKey = QStringLiteral("test-key");
    config.model = QStringLiteral("test-model");
    config.baseUrl = baseUrl;
    config.authStyle = authStyle;
    config.validationMethod = validationMethod;
    config.providerId = providerId;
    return config;
}

}  // namespace

void TstProvider::initTestCase()
{
    // QSignalSpy 需要能构造 CredentialStatus 参数
    qRegisterMetaType<CredentialStatus>("CredentialStatus");
}

void TstProvider::unconfiguredProviderIsReported()
{
    OpenAiProvider provider;
    QVERIFY(!provider.isConfigured());
    QCOMPARE(provider.providerId(), QStringLiteral("openai"));
    QCOMPARE(provider.protocol(), AiProvider::OpenAI);
}

void TstProvider::configureMarksProviderReady()
{
    AnthropicProvider provider;
    provider.configure(readyConfig());

    QVERIFY(provider.isConfigured());
    QCOMPARE(provider.providerId(), QStringLiteral("anthropic"));
}

void TstProvider::factoryCreatesExpectedProtocols()
{
    std::unique_ptr<AiProvider> openAi = ProviderFactory::create(AiProvider::OpenAI);
    QVERIFY(openAi != nullptr);
    QCOMPARE(openAi->providerId(), QStringLiteral("openai"));

    std::unique_ptr<AiProvider> anthropic = ProviderFactory::create(AiProvider::Anthropic);
    QVERIFY(anthropic != nullptr);
    QCOMPARE(anthropic->providerId(), QStringLiteral("anthropic"));

    QVERIFY(ProviderFactory::create(static_cast<AiProvider::Protocol>(99)) == nullptr);
}

void TstProvider::missingKeyFailsStructured()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);

    provider.sendChat(textRequest(QStringLiteral("hi")));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::NotConfigured);
    QCOMPARE(transport.m_requests.size(), 0);
}

void TstProvider::streamSuccessProducesDeltasAndFinished()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy deltaSpy(&provider, &AiProvider::chatDelta);
    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);
    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);

    provider.sendChat(textRequest(QStringLiteral("hi")));
    QCOMPARE(transport.m_requests.size(), 1);
    const int handle = transport.m_lastHandle;

    transport.pushChunk(handle, "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n");
    transport.pushChunk(handle, "data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\n\n");
    transport.pushChunk(handle, "data: [DONE]\n\n");
    transport.pushFinished(handle);

    QCOMPARE(deltaSpy.count(), 2);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);

    const ChatResponse response = finishedSpy.at(0).at(0).value<ChatResponse>();
    QVERIFY(response.ok);
    QCOMPARE(response.text, QStringLiteral("Hello"));
}

void TstProvider::nonStreamSuccessParsesBodyAndUsage()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);

    provider.sendChat(textRequest(QStringLiteral("hi"), false));
    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle,
                        "{\"model\":\"gpt-test\",\"choices\":[{\"message\":{\"content\":\"Hi\"}}],"
                        "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}");
    transport.pushFinished(handle);

    QCOMPARE(finishedSpy.count(), 1);
    const ChatResponse response = finishedSpy.at(0).at(0).value<ChatResponse>();
    QVERIFY(response.ok);
    QCOMPARE(response.text, QStringLiteral("Hi"));
    QCOMPARE(response.usage.totalTokens, 3);
    QCOMPARE(response.model, QStringLiteral("gpt-test"));
}

void TstProvider::unauthorizedIsReported()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    provider.sendChat(textRequest(QStringLiteral("hi")));
    transport.pushFailed(transport.m_lastHandle, 401, NetworkTransport::Failure::Other,
                         QStringLiteral("unauthorized"));

    QCOMPARE(failedSpy.count(), 1);
    const ChatError error = failedSpy.at(0).at(0).value<ChatError>();
    QCOMPARE(error.code, ChatError::Unauthorized);
    QCOMPARE(error.httpStatus, 401);
}

void TstProvider::rateLimitedIsReported()
{
    FakeNetworkTransport transport;
    AnthropicProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    provider.sendChat(textRequest(QStringLiteral("hi")));
    transport.pushFailed(transport.m_lastHandle, 429, NetworkTransport::Failure::Other,
                         QStringLiteral("rate limited"));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::RateLimited);
}

void TstProvider::timeoutIsReported()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    provider.sendChat(textRequest(QStringLiteral("hi")));
    transport.pushFailed(transport.m_lastHandle, 0, NetworkTransport::Failure::Timeout,
                         QStringLiteral("timed out"));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::Timeout);
}

void TstProvider::networkUnavailableIsReported()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    provider.sendChat(textRequest(QStringLiteral("hi")));
    transport.pushFailed(transport.m_lastHandle, 0, NetworkTransport::Failure::NetworkUnavailable,
                         QStringLiteral("no route to host"));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::NetworkUnavailable);
}

void TstProvider::streamInterruptedIsReported()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy deltaSpy(&provider, &AiProvider::chatDelta);
    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);

    provider.sendChat(textRequest(QStringLiteral("hi")));
    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle, "data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n");
    transport.pushFailed(handle, 0, NetworkTransport::Failure::Other,
                         QStringLiteral("connection reset"));

    QCOMPARE(deltaSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(failedSpy.at(0).at(0).value<ChatError>().code != ChatError::None);
}

void TstProvider::emptyStreamIsReported()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);

    provider.sendChat(textRequest(QStringLiteral("hi")));
    transport.pushFinished(transport.m_lastHandle);

    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::EmptyResponse);
}

void TstProvider::cancelAbortsInflightRequest()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    provider.sendChat(textRequest(QStringLiteral("hi")));
    const int handle = transport.m_lastHandle;

    provider.cancel();

    QVERIFY(transport.m_abortedHandles.contains(handle));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::Cancelled);

    // 中断后传输层再回调也不应影响已结束的请求
    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);
    transport.pushFinished(handle);
    QCOMPARE(finishedSpy.count(), 0);
}

void TstProvider::imagesRequireConfirmation()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);

    ChatRequest request = textRequest(QStringLiteral("describe"));
    ChatImage image;
    image.bytes = QByteArrayLiteral("fake-image-bytes");
    image.mimeType = QStringLiteral("image/png");
    request.messages[0].images.append(image);

    provider.sendChat(request);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).value<ChatError>().code, ChatError::ImagesNotConfirmed);
    QCOMPARE(transport.m_requests.size(), 0);
}

void TstProvider::openAiSendsImageAsDataUri()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    ChatRequest request = textRequest(QStringLiteral("describe"));
    request.imagesConfirmed = true;
    ChatImage image;
    image.bytes = QByteArrayLiteral("fake-image-bytes");
    image.mimeType = QStringLiteral("image/png");
    request.messages[0].images.append(image);

    provider.sendChat(request);

    QCOMPARE(transport.m_requests.size(), 1);
    const QString body = QString::fromUtf8(transport.m_requests.first().body);
    QVERIFY(body.contains(QStringLiteral("image_url")));
    QVERIFY(body.contains(QStringLiteral("data:image/png;base64,")));

    provider.cancel();
}

void TstProvider::anthropicSendsImageAsBase64Source()
{
    FakeNetworkTransport transport;
    AnthropicProvider provider(&transport);
    provider.configure(readyConfig());

    ChatRequest request = textRequest(QStringLiteral("describe"));
    request.imagesConfirmed = true;
    ChatImage image;
    image.bytes = QByteArrayLiteral("fake-image-bytes");
    image.mimeType = QStringLiteral("image/png");
    request.messages[0].images.append(image);

    provider.sendChat(request);

    QCOMPARE(transport.m_requests.size(), 1);
    const QString body = QString::fromUtf8(transport.m_requests.first().body);
    QVERIFY(body.contains(QStringLiteral("media_type")));
    QVERIFY(body.contains(QStringLiteral("base64")));

    provider.cancel();
}

void TstProvider::customProviderUsesConfiguredBaseUrlAndBearer()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://my.gateway.example/v1/"),
                                    QStringLiteral("bearer"), QStringLiteral("chat-minimal"),
                                    QStringLiteral("custom")));

    provider.sendChat(textRequest(QStringLiteral("hi")));

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    // 末尾斜杠被归一化，路径正确拼接
    QCOMPARE(request.url, QStringLiteral("https://my.gateway.example/v1/chat/completions"));
    QCOMPARE(headerValue(request, QStringLiteral("Authorization")),
             QStringLiteral("Bearer test-key"));

    provider.cancel();
}

void TstProvider::perVendorHeadersFollowAuthStyle()
{
    // bearer：Authorization 头，且不出现 x-api-key
    {
        FakeNetworkTransport transport;
        OpenAiProvider provider(&transport);
        provider.configure(vendorConfig(QStringLiteral("https://api.deepseek.com"),
                                        QStringLiteral("bearer"), QStringLiteral("models"),
                                        QStringLiteral("deepseek")));
        provider.sendChat(textRequest(QStringLiteral("hi")));

        QCOMPARE(transport.m_requests.size(), 1);
        const NetworkTransport::Request request = transport.m_requests.first();
        QCOMPARE(request.url, QStringLiteral("https://api.deepseek.com/chat/completions"));
        QVERIFY(headerValue(request, QStringLiteral("x-api-key")).isEmpty());
        provider.cancel();
    }

    // x-api-key：改用厂商专属鉴权头
    {
        FakeNetworkTransport transport;
        OpenAiProvider provider(&transport);
        provider.configure(vendorConfig(QStringLiteral("https://open.bigmodel.cn/api/paas/v4"),
                                        QStringLiteral("x-api-key"), QStringLiteral("chat-minimal"),
                                        QStringLiteral("zhipu")));
        provider.sendChat(textRequest(QStringLiteral("hi")));

        const NetworkTransport::Request request = transport.m_requests.first();
        QCOMPARE(headerValue(request, QStringLiteral("x-api-key")), QStringLiteral("test-key"));
        QVERIFY(headerValue(request, QStringLiteral("Authorization")).isEmpty());
        provider.cancel();
    }
}

void TstProvider::anthropicAlwaysSendsApiKeyAndVersion()
{
    FakeNetworkTransport transport;
    AnthropicProvider provider(&transport);
    // 刻意**不给** authStyle / extraHeaders：验证 Provider 自身的兜底行为
    provider.configure(vendorConfig(QStringLiteral("https://api.anthropic.com/v1"),
                                    QString(), QStringLiteral("models"),
                                    QStringLiteral("anthropic")));

    provider.sendChat(textRequest(QStringLiteral("hi")));

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(headerValue(request, QStringLiteral("x-api-key")), QStringLiteral("test-key"));
    QCOMPARE(headerValue(request, QStringLiteral("anthropic-version")),
             QStringLiteral("2023-06-01"));
    QVERIFY(headerValue(request, QStringLiteral("Authorization")).isEmpty());

    provider.cancel();
}

void TstProvider::nativeParamsAreInjectedButNotOverridingCoreFields()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.deepseek.com"),
                                    QStringLiteral("bearer"), QStringLiteral("models"),
                                    QStringLiteral("deepseek")));

    ChatRequest request = textRequest(QStringLiteral("hi"));
    request.thinkingLevel = QStringLiteral("high");
    // 同时给出**保留字段**，验证不会被覆盖
    request.nativeParams = QJsonObject{
        {QStringLiteral("reasoning_effort"), QStringLiteral("high")},
        {QStringLiteral("model"), QStringLiteral("evil")},
        {QStringLiteral("messages"), QStringLiteral("evil")},
    };
    provider.sendChat(request);

    QCOMPARE(transport.m_requests.size(), 1);
    const QJsonObject body =
        QJsonDocument::fromJson(transport.m_requests.first().body).object();
    QCOMPARE(body.value(QStringLiteral("reasoning_effort")).toString(), QStringLiteral("high"));
    QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test-model"));
    QVERIFY(body.value(QStringLiteral("messages")).isArray());

    provider.cancel();
}

void TstProvider::validateReportsValidOnSuccess()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.openai.com/v1"),
                                    QStringLiteral("bearer"), QStringLiteral("models"),
                                    QStringLiteral("openai")));

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();

    QCOMPARE(transport.m_requests.size(), 1);
    QCOMPARE(transport.m_requests.first().method, QStringLiteral("GET"));
    QCOMPARE(transport.m_requests.first().url, QStringLiteral("https://api.openai.com/v1/models"));

    transport.pushFinished(transport.m_lastHandle);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::Valid);
}

void TstProvider::validateReportsInvalidOnUnauthorized()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.openai.com/v1"),
                                    QStringLiteral("bearer"), QStringLiteral("models"),
                                    QStringLiteral("openai")));

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();
    transport.pushFailed(transport.m_lastHandle, 401, NetworkTransport::Failure::Other,
                         QStringLiteral("unauthorized"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::Invalid);
}

void TstProvider::validateReportsUnverifiableOnRateLimit()
{
    FakeNetworkTransport transport;
    AnthropicProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.anthropic.com/v1"),
                                    QStringLiteral("x-api-key"), QStringLiteral("models"),
                                    QStringLiteral("anthropic")));

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();
    transport.pushFailed(transport.m_lastHandle, 429, NetworkTransport::Failure::Other,
                         QStringLiteral("rate limited"));

    QCOMPARE(spy.count(), 1);
    // 限流**绝不能**被当成 Key 无效
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::Unverifiable);
}

void TstProvider::validateReportsUnverifiableOnTimeout()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.openai.com/v1"),
                                    QStringLiteral("bearer"), QStringLiteral("models"),
                                    QStringLiteral("openai")));

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();
    transport.pushFailed(transport.m_lastHandle, 0, NetworkTransport::Failure::Timeout,
                         QStringLiteral("timed out"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::Unverifiable);
}

void TstProvider::validateReportsUnverifiableOnNetworkFailure()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.openai.com/v1"),
                                    QStringLiteral("bearer"), QStringLiteral("models"),
                                    QStringLiteral("openai")));

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();
    transport.pushFailed(transport.m_lastHandle, 0, NetworkTransport::Failure::NetworkUnavailable,
                         QStringLiteral("no route to host"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::Unverifiable);
}

void TstProvider::validateReportsNotConfiguredWithoutKey()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::NotConfigured);
    QCOMPARE(transport.m_requests.size(), 0);
}

void TstProvider::validateUsesChatMinimalWhenMethodSaysSo()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    // 无 /models 的厂商（如智谱）按 catalog 声明走最小对话请求
    provider.configure(vendorConfig(QStringLiteral("https://open.bigmodel.cn/api/paas/v4"),
                                    QStringLiteral("bearer"), QStringLiteral("chat-minimal"),
                                    QStringLiteral("zhipu")));

    QSignalSpy spy(&provider, &AiProvider::credentialsChecked);
    provider.validateCredentials();

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.url, QStringLiteral("https://open.bigmodel.cn/api/paas/v4/chat/completions"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 1);
    QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);

    transport.pushFinished(transport.m_lastHandle);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CredentialStatus>(), CredentialStatus::Valid);
}

void TstProvider::fetchModelsParsesVendorList()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://api.deepseek.com"),
                                    QStringLiteral("bearer"), QStringLiteral("models"),
                                    QStringLiteral("deepseek")));

    QSignalSpy spy(&provider, &AiProvider::modelsFetched);
    provider.fetchModels();

    QCOMPARE(transport.m_requests.size(), 1);
    QCOMPARE(transport.m_requests.first().url, QStringLiteral("https://api.deepseek.com/models"));

    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle, "{\"data\":[{\"id\":\"deepseek-v3\"},{\"id\":\"deepseek-r1\"},"
                                "{\"id\":\"deepseek-v3\"}]}");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    const QStringList ids = spy.at(0).at(0).value<QStringList>();
    QCOMPARE(ids, QStringList({QStringLiteral("deepseek-v3"), QStringLiteral("deepseek-r1")}));
    QVERIFY(spy.at(0).at(1).toString().isEmpty());
}

void TstProvider::fetchModelsRejectsUnsupportedVendor()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(vendorConfig(QStringLiteral("https://open.bigmodel.cn/api/paas/v4"),
                                    QStringLiteral("bearer"), QStringLiteral("chat-minimal"),
                                    QStringLiteral("zhipu")));

    QSignalSpy spy(&provider, &AiProvider::modelsFetched);
    provider.fetchModels();

    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());
    QCOMPARE(transport.m_requests.size(), 0);
}

QTEST_GUILESS_MAIN(TstProvider)
#include "tst_provider.moc"