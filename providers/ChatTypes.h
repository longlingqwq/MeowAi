#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * AI 请求 / 响应的公共数据结构。
 * 由 providers/ 层持有，core 与 ui 只依赖这些类型，不依赖具体厂商协议。
 */

/// 多模态输入：图片可给本地路径或原始字节（功能 #4）
struct ChatImage
{
    QString path;      ///< 本地图片路径，可为空
    QByteArray bytes;  ///< 原始字节，可为空
    QString mimeType;  ///< 如 "image/png"；为空时按扩展名推断
};

/// 模型发起的一次工具调用（OpenAI 兼容 function calling）
struct ChatToolCall
{
    QString id;         ///< 厂商回传的调用 id；回填 tool 结果时必须**原样带回**
    QString name;       ///< 工具名（本模块固定为 "web_search"）
    QString arguments;  ///< JSON 字符串形式的参数（如 {"query":"…"}）

    bool isValid() const { return !name.isEmpty(); }
};

struct ChatMessage
{
    enum Role { System, User, Assistant, Tool };

    Role role = User;
    QString text;
    QList<ChatImage> images;

    /// role == Tool 时：对应 assistant 那轮 tool_calls 里的调用 id（回填结果必须携带）
    QString toolCallId;
    /// role == Assistant 且模型发起工具调用时：本轮待执行的调用列表
    QList<ChatToolCall> toolCalls;
};

struct ChatRequest
{
    QString model;
    QList<ChatMessage> messages;
    double temperature = 0.7;
    int maxTokens = 0;  ///< 0 表示使用厂商默认

    /// 是否使用 SSE 流式；本阶段默认流式，便于 UI 增量渲染与验证中断能力
    bool stream = true;

    /// 图片上传的**显式确认开关**：仅当为 true 且消息含图片时，才允许把图片发给厂商；
    /// 默认 false，杜绝静默上传（见 docs/PROMPT.md 硬性约束）。
    bool imagesConfirmed = false;

    /// 统一思考档位（off / low / medium / high）；空串表示不注入任何思考参数。
    /// 具体原生字段由 catalog 的 thinkingSpec.native 映射，provider 只负责透传。
    QString thinkingLevel;

    /// 厂商原生参数片段（由 catalog 档位映射得到，或用户在高级区手改）；
    /// provider 会将其合并进请求体，但**不得覆盖** model / messages / stream / system。
    QJsonObject nativeParams;

    /// 可用的工具清单（OpenAI 兼容 function 描述数组）；为空表示不带工具（普通对话）
    QJsonArray tools;
    /// 工具选择策略（"auto" / "none"…）；为空表示不下发该字段
    QString toolChoice;
};

/// Key 有效性校验的三态语义（**限流 / 断网绝不允许被判成「无效」**）
enum class CredentialStatus {
    Unknown = 0,    ///< 尚未校验
    NotConfigured,  ///< 未填写 Key，无从校验
    Valid,          ///< 厂商返回 2xx：Key 可用
    Invalid,        ///< 厂商返回 401 / 403：Key 无效或无权限
    Unverifiable,   ///< 429 / 超时 / 断网 / 5xx：无法判定，不能当作无效
};

/// Token 用量统计；厂商未返回相关字段时为 -1
struct ChatUsage
{
    int promptTokens = -1;
    int completionTokens = -1;
    int totalTokens = -1;
};

/**
 * 结构化错误：让上层无需解析字符串即可区分常见失败原因，
 * 并统一承载 HTTP 状态码（401 / 429 / 5xx 等）。
 */
struct ChatError
{
    enum Code {
        None = 0,           ///< 无错误
        NotConfigured,      ///< 缺少 API Key 等必要配置
        ImagesNotConfirmed, ///< 请求含图片但未获用户显式确认
        NetworkUnavailable, ///< 平台判定为断网
        Timeout,            ///< 请求超时
        Unauthorized,       ///< HTTP 401 / 403：Key 无效或无权限
        RateLimited,        ///< HTTP 429：限流
        ServerError,        ///< HTTP 5xx：服务端错误
        HttpError,          ///< 其它非 2xx 状态
        StreamInterrupted,  ///< 流式响应中途断开
        EmptyResponse,      ///< 请求成功但无有效内容
        InvalidResponse,    ///< 响应体无法解析
        Cancelled,          ///< 被本地 cancel() 中断
        Unknown,
    };

    Code code = None;
    int httpStatus = 0;  ///< HTTP 状态码，未知为 0
    QString message;     ///< 面向用户的说明；**已脱敏**，禁止包含 Key 或完整请求体

    bool isError() const { return code != None; }
};

struct ChatResponse
{
    /// 收尾原因：用于判断本轮是「最终回答」还是「模型要求调用工具」
    enum FinishReason {
        NotSpecified = 0,  ///< 厂商未给出（如非流式缺字段）
        Stop,              ///< 正常结束
        ToolCalls,         ///< 模型要求调用工具（需继续一轮）
        Length,            ///< 达到长度上限
        Other,
    };

    bool ok = false;
    QString text;
    QString model;    ///< 厂商回传的实际模型标识
    ChatUsage usage;
    QString rawJson;  ///< 原始响应（已脱敏），仅用于排查
    ChatError error;  ///< ok == false 时说明失败原因

    /// 模型本轮回传的工具调用；非空时上层应执行工具并再发一轮
    QList<ChatToolCall> toolCalls;
    FinishReason finishReason = NotSpecified;
};

/// Provider 连接配置；apiKey 由 storage/SecureStore 提供，禁止明文落盘
struct ProviderConfig
{
    QString apiKey;
    QString baseUrl;            ///< 为空则使用协议默认域名
    QString model;
    QString providerId;         ///< catalog 厂商 id：仅用于提示语，不含敏感信息
    QString authStyle;          ///< bearer | x-api-key | query-key（空 = bearer）
    QString validationMethod;   ///< models | chat-minimal（空 = chat-minimal）
    QJsonObject extraHeaders;   ///< 随请求固定携带的额外头部（如 anthropic-version）
};

Q_DECLARE_METATYPE(ChatError)
Q_DECLARE_METATYPE(ChatResponse)
Q_DECLARE_METATYPE(CredentialStatus)
Q_DECLARE_METATYPE(ChatToolCall)
Q_DECLARE_METATYPE(QList<ChatToolCall>)
