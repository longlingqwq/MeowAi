#pragma once

#include "providers/AiProvider.h"

/**
 * Anthropic 协议实现（桩），行为约定与 OpenAiProvider 一致。
 */
class AnthropicProvider : public AiProvider
{
    Q_OBJECT

public:
    explicit AnthropicProvider(QObject *parent = nullptr);

    Protocol protocol() const override;
    QString providerId() const override;
    bool isConfigured() const override;
    void configure(const ProviderConfig &config) override;
    void sendChat(const ChatRequest &request) override;
    void cancel() override;

private:
    ProviderConfig m_config;
};
