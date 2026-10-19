#include "providers/AnthropicProvider.h"
#include "providers/OpenAiProvider.h"
#include "providers/ProviderFactory.h"
#include "tests/fakes/FakeNetworkTransport.h"

#include <QtTest>

#include <QFile>
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

}  // namespace

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

QTEST_GUILESS_MAIN(TstProvider)
#include "tst_provider.moc"
