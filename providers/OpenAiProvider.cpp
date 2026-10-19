#include "providers/OpenAiProvider.h"

OpenAiProvider::OpenAiProvider(QObject *parent)
    : AiProvider(parent)
{
}

AiProvider::Protocol OpenAiProvider::protocol() const
{
    return Protocol::OpenAI;
}

QString OpenAiProvider::providerId() const
{
    return QStringLiteral("openai");
}

bool OpenAiProvider::isConfigured() const
{
    return !m_config.apiKey.isEmpty();
}

void OpenAiProvider::configure(const ProviderConfig &config)
{
    m_config = config;
}

void OpenAiProvider::sendChat(const ChatRequest &request)
{
    Q_UNUSED(request)
    // 桩实现：不发请求，直接给出明确错误，保证上层与测试都不会崩溃
    emit chatFailed(QStringLiteral("OpenAI 协议请求尚未实现（阶段二接入）"));
}

void OpenAiProvider::cancel()
{
}
