#pragma once

#include "providers/AiProvider.h"
#include "providers/NetworkTransport.h"
#include "providers/SseStreamParser.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>

/**
 * OpenAI Chat Completions 协议实现（真实网络请求，含 SSE 流式与多模态）。
 *
 * 安全：Key 仅来自 configure() 传入的 ProviderConfig；日志禁止打印 Key 与请求体。
 * 职责：只做协议编解码，网络字节收发交给 NetworkTransport（可注入假实现用于单测）。
 *
 * v0.2.4：支持 function calling —— 请求体下发 tools / tool_choice，
 * 流式累积 delta.tool_calls（按 index 拼接 id / name / arguments），
 * 并在 chatFinished 里回传 toolCalls 与 finishReason，交由 core 层驱动工具循环。
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
    void validateCredentials() override;
    void fetchModels() override;

private:
    bool buildBody(const ChatRequest &request, QJsonObject *body, QString *error) const;
    /// 处理一条流式事件；返回 false 表示已发出错误信号，调用方应停止
    bool processStreamEvent(const SseEvent &event);
    void handleChunk(int handle, const QByteArray &chunk);
    void handleFinished(int handle);
    void handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure, const QString &errorText);
    void fail(ChatError::Code code, int httpStatus, const QString &message);
    void abortActive();

    /// 组装 Key 校验 / 模型清单请求所需的公共头部与地址
    NetworkTransport::Request auxRequest(const QString &path, const QString &method) const;
    /// 结束校验：以三态结论回传
    void finishValidation(CredentialStatus status, const QString &message);
    /// 解析 `GET /models` 响应体里的模型 id
    QStringList parseModelIds() const;

    /// 把一次工具调用的 JSON 记入（流式按 index 拼接、非流式整包写入）
    void accumulateToolCall(const QJsonObject &object);
    /// finish_reason 字符串 → 枚举
    static ChatResponse::FinishReason finishReasonFrom(const QString &value);
    /// 汇总本轮累积的工具调用（按 index 升序）
    QList<ChatToolCall> collectToolCalls() const;

    ProviderConfig m_config;
    NetworkTransport *m_transport = nullptr;

    int m_handle = 0;          ///< 对话请求句柄，0 表示无
    int m_validateHandle = 0;  ///< Key 校验句柄
    int m_modelsHandle = 0;    ///< 模型清单拉取句柄
    QByteArray m_auxBody;      ///< 校验 / 清单请求的响应体
    bool m_auxIsGet = false;   ///< 辅助请求是否为 GET（决定校验语义）

    bool m_streaming = true;   ///< 当前对话请求是否为流式
    SseStreamParser m_sse;
    QString m_accumulated;     ///< 流式累积文本
    QByteArray m_rawBody;      ///< 非流式响应体
    ChatUsage m_usage;
    QHash<int, ChatToolCall> m_toolCalls;  ///< 流式 tool_calls：index -> 调用
    ChatResponse::FinishReason m_finishReason = ChatResponse::NotSpecified;
};
