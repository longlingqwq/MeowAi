#include "providers/AnthropicProvider.h"
#include "providers/OpenAiProvider.h"
#include "providers/ProviderFactory.h"

#include <QtTest>

/**
 * Provider 抽象层测试：未配置、空请求、工厂选择都必须给出结果而非崩溃。
 */
class TstProvider : public QObject
{
    Q_OBJECT

private slots:
    void unconfiguredProviderIsReported();
    void configureMarksProviderReady();
    void stubSendChatFailsWithSignal();
    void stubSendChatToleratesEmptyRequest();
    void factoryCreatesExpectedProtocols();
};

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
    ProviderConfig config;
    config.apiKey = QStringLiteral("test-key");
    config.model = QStringLiteral("claude-test");
    provider.configure(config);

    QVERIFY(provider.isConfigured());
    QCOMPARE(provider.providerId(), QStringLiteral("anthropic"));
}

void TstProvider::stubSendChatFailsWithSignal()
{
    OpenAiProvider provider;
    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);

    ChatRequest request;
    request.model = QStringLiteral("gpt-test");
    request.messages.append({ChatMessage::User, QStringLiteral("hi"), {}});
    provider.sendChat(request);

    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(!failedSpy.at(0).at(0).toString().isEmpty());
}

void TstProvider::stubSendChatToleratesEmptyRequest()
{
    AnthropicProvider provider;
    QSignalSpy failedSpy(&provider, &AiProvider::chatFailed);
    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);

    provider.sendChat(ChatRequest{});
    provider.cancel();

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
}

void TstProvider::factoryCreatesExpectedProtocols()
{
    std::unique_ptr<AiProvider> openAi(ProviderFactory::create(AiProvider::OpenAI));
    QVERIFY(openAi != nullptr);
    QCOMPARE(openAi->providerId(), QStringLiteral("openai"));

    std::unique_ptr<AiProvider> anthropic(ProviderFactory::create(AiProvider::Anthropic));
    QVERIFY(anthropic != nullptr);
    QCOMPARE(anthropic->providerId(), QStringLiteral("anthropic"));
}

QTEST_GUILESS_MAIN(TstProvider)
#include "tst_provider.moc"
