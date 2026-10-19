#pragma once

#include "providers/AiProvider.h"
#include "providers/NetworkTransport.h"
#include "providers/SseStreamParser.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

/**
 * OpenAI Chat Completions 协议实现（真实网络请求，含 SSE 流式与多模态）。
 *
 * 安全：Key 仅来自 configure() 传入的 ProviderConfig；日志禁止打印 Key 与请求体。
 * 职责：只做协议编解码，网络字节收发交给 NetworkTransport（可注入假实现用于单测）。
 */
class OpenAiProvider : public AiProvider
{
    Q_OBJECT

public:
    /// transport 为空时自建 QNetworkTransport（父对象为本对象）；注入时所有权归调用方
    explicit OpenAiProvider(NetworkTransport *transport = nullptr, QObject *parent = nullptr);

    Protocol protocol() const override;
    QString providerId() const override;
    bool isConfigured() const override;
    void configure(const ProviderConfig &config) override;
    void sendChat(const ChatRequest &request) override;
    void cancel() override;

private:
    bool buildBody(const ChatRequest &request, QJsonObject *body, QString *error) const;
    /// 处理一条流式事件；返回 false 表示已发出错误信号，调用方应停止
    bool processStreamEvent(const SseEvent &event);
    void handleChunk(int handle, const QByteArray &chunk);
    void handleFinished(int handle);
    void handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure, const QString &errorText);
    void fail(ChatError::Code code, int httpStatus, const QString &message);
    void abortActive();

    ProviderConfig m_config;
    NetworkTransport *m_transport = nullptr;

    int m_handle = 0;          ///< 在途请求句柄，0 表示无
    bool m_streaming = true;   ///< 当前请求是否为流式
    SseStreamParser m_sse;
    QString m_accumulated;     ///< 流式累积文本
    QByteArray m_rawBody;      ///< 非流式响应体
    ChatUsage m_usage;
};
