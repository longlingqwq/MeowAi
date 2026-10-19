#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

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

struct ChatMessage
{
    enum Role { System, User, Assistant };

    Role role = User;
    QString text;
    QList<ChatImage> images;
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
    bool ok = false;
    QString text;
    QString model;    ///< 厂商回传的实际模型标识
    ChatUsage usage;
    QString rawJson;  ///< 原始响应（已脱敏），仅用于排查
    ChatError error;  ///< ok == false 时说明失败原因
};

/// Provider 连接配置；apiKey 由 storage/SecureStore 提供，禁止明文落盘
struct ProviderConfig
{
    QString apiKey;
    QString baseUrl;  ///< 为空则使用厂商默认域名
    QString model;
};

Q_DECLARE_METATYPE(ChatError)
Q_DECLARE_METATYPE(ChatResponse)
