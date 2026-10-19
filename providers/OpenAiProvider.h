#pragma once

#include "providers/AiProvider.h"

/**
 * OpenAI 协议实现（桩）。
 * 阶段一只保证接口与配置行为正确，sendChat 一律返回“未实现”错误，不发起网络请求。
 */
class OpenAiProvider : public AiProvider
{
    Q_OBJECT

public:
    explicit OpenAiProvider(QObject *parent = nullptr);

    Protocol protocol() const override;
    QString providerId() const override;
    bool isConfigured() const override;
    void configure(const ProviderConfig &config) override;
    void sendChat(const ChatRequest &request) override;
    void cancel() override;

private:
    ProviderConfig m_config;
};
