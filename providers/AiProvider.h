#pragma once

#include "providers/ChatTypes.h"

#include <QObject>

/**
 * AI 接入抽象接口（功能 #3 / #4）。
 *
 * 屏蔽 OpenAI 与 Anthropic 的协议差异：core / ui 只依赖本接口，
 * 所有网络请求一律由 providers/ 层发出（见 docs/AGENT.md §2）。
 */
class AiProvider : public QObject
{
    Q_OBJECT

public:
    enum Protocol { OpenAI, Anthropic };
    Q_ENUM(Protocol)

    explicit AiProvider(QObject *parent = nullptr);

    ~AiProvider() override = default;

    virtual Protocol protocol() const = 0;
    virtual QString providerId() const = 0;

    /// 是否已具备发起请求的必要配置（不含有效性校验）
    virtual bool isConfigured() const = 0;
    virtual void configure(const ProviderConfig &config) = 0;

    /// 异步发起对话，结果通过信号返回，绝不阻塞 UI 线程
    virtual void sendChat(const ChatRequest &request) = 0;

    /// 取消进行中的请求；无请求时为空操作。**必须真正中断在途网络请求**
    virtual void cancel() = 0;

signals:
    void chatDelta(const QString &delta);             ///< 流式分片（增量文本）
    void chatFinished(const ChatResponse &response);  ///< 完整结果（成功）
    void chatFailed(const ChatError &error);          ///< 结构化失败原因
};
