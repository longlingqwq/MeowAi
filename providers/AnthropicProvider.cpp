#include "providers/AnthropicProvider.h"

AnthropicProvider::AnthropicProvider(QObject *parent)
    : AiProvider(parent)
{
}

AiProvider::Protocol AnthropicProvider::protocol() const
{
    return Protocol::Anthropic;
}

QString AnthropicProvider::providerId() const
{
    return QStringLiteral("anthropic");
}

bool AnthropicProvider::isConfigured() const
{
    return !m_config.apiKey.isEmpty();
}

void AnthropicProvider::configure(const ProviderConfig &config)
{
    m_config = config;
}

void AnthropicProvider::sendChat(const ChatRequest &request)
{
    Q_UNUSED(request)
    emit chatFailed(QStringLiteral("Anthropic 协议请求尚未实现（阶段二接入）"));
}

void AnthropicProvider::cancel()
{
}
