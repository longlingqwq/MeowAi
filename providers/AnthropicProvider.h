#pragma once

#include "providers/AiProvider.h"
#include "providers/NetworkTransport.h"
#include "providers/SseStreamParser.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

/**
 * Anthropic Messages API 协议实现（真实网络请求，含 SSE 流式与多模态）。
 *
 * 与 OpenAI 的差异：鉴权头为 x-api-key、需 anthropic-version、system 为顶层字段、
 * max_tokens 必填、内容块采用 image/source.base64 形式。
 */
class AnthropicProvider : public AiProvider
{
    Q_OBJECT

public:
    /// transport 为空时自建 QNetworkTransport（父对象为本对象）；注入时所有权归调用方
    explicit AnthropicProvider(NetworkTransport *transport = nullptr, QObject *parent = nullptr);

    Protocol protocol() const override;
    QString providerId() const override;
    bool isConfigured() const override;
    void configure(const ProviderConfig &config) override;
    void sendChat(const ChatRequest &request) override;
    void cancel() override;

private:
    bool buildBody(const ChatRequest &request, QJsonObject *body, QString *error) const;
    bool processStreamEvent(const SseEvent &event);
    void handleChunk(int handle, const QByteArray &chunk);
    void handleFinished(int handle);
    void handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure, const QString &errorText);
    void fail(ChatError::Code code, int httpStatus, const QString &message);
    void abortActive();

    ProviderConfig m_config;
    NetworkTransport *m_transport = nullptr;

    int m_handle = 0;
    bool m_streaming = true;
    SseStreamParser m_sse;
    QString m_accumulated;
    QByteArray m_rawBody;
    ChatUsage m_usage;
    QString m_model;
};
