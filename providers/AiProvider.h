#pragma once

#include "providers/ChatTypes.h"

#include <QHash>
#include <QObject>
#include <QStringList>

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

    /**
     * 最近一次拉取模型清单时厂商返回的上下文窗口（模型 id → tokens）。
     *
     * 只有部分厂商（OpenRouter / Together / Groq / Gemini 等）会在 `/models`
     * 响应里给出该字段；主流国内厂商只返回 id，此时返回空表，由 providers.json
     * 内置值兜底。默认实现为空表。
     */
    virtual QHash<QString, int> modelContextWindows() const { return {}; }

    /// 取消进行中的请求；无请求时为空操作。**必须真正中断在途网络请求**
    virtual void cancel() = 0;

    /**
     * 异步校验 API Key 有效性（**手动触发，不自动轮询**）。
     * 请求一律经 NetworkTransport 发出，便于注入假实现离线单测。
     * 结果通过 credentialsChecked 回传，语义遵循 CredentialStatus 三态：
     * 2xx → Valid；401/403 → Invalid；429 / 超时 / 断网 / 5xx → Unverifiable。
     */
    virtual void validateCredentials() = 0;

    /**
     * 异步拉取厂商模型清单（仅 validation.method == "models" 的厂商有实现）。
     * **失败不得视为致命错误**：结果通过 modelsFetched 回传，失败时 error 非空、列表为空，
     * 由上层保留内置精选清单。
     */
    virtual void fetchModels() = 0;

    /**
     * 为「资料库服务」（embedding / caption）拉取模型清单。
     *
     * 与 `fetchModels()` 的区别：**忽略 catalog 的 validationMethod**，一律尝试
     * `GET {baseUrl}/models`。资料库允许用户选择非对话用途的厂商（如 chat-minimal），
     * 是否支持 `/models` 由实际响应决定，因此不能沿用聊天侧的静态判断。
     * 失败语义同 `fetchModels()`：空列表 + 明确原因，绝不抛异常。
     * 默认实现直接回「该厂商不支持动态拉取模型清单」。
     */
    virtual void fetchModelsForService();

signals:
    void chatDelta(const QString &delta);             ///< 流式分片（增量文本）
    void chatFinished(const ChatResponse &response);  ///< 完整结果（成功）
    void chatFailed(const ChatError &error);          ///< 结构化失败原因
    /// Key 校验结果；message 为面向用户的说明（已脱敏），可为空
    void credentialsChecked(CredentialStatus status, const QString &message);
    /// 动态模型清单；失败时 modelIds 为空、error 非空
    void modelsFetched(const QStringList &modelIds, const QString &error);
};
