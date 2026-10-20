#pragma once

#include "providers/AiProvider.h"
#include "providers/ChatTypes.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

/**
 * Provider 公共工具：默认域名、统一鉴权 / 请求头、URL 拼接、原生参数合并、图片读取 / 编码、日志脱敏。
 * 不含任何厂商专属协议细节，供 OpenAiProvider / AnthropicProvider 共用。
 */
namespace ProviderUtils {

/// 厂商默认 API 根地址（不含具体端点路径）
QString defaultBaseUrl(AiProvider::Protocol protocol);

/// 解析生效的 API 根地址：config.baseUrl 优先，末尾 '/' 会被去除
QString resolveBaseUrl(const ProviderConfig &config, AiProvider::Protocol protocol);

/**
 * 拼接端点 URL：`resolveBaseUrl + path`。
 * authStyle == "query-key" 时把 Key 作为 `key` 查询参数追加（此为该鉴权风格的定义）。
 * 注意：返回串可能含 Key，**禁止写日志**。
 */
QString endpointUrl(const ProviderConfig &config, AiProvider::Protocol protocol, const QString &path);

/**
 * 统一请求头：Content-Type / Accept + 按 authStyle 生成的鉴权头 + extraHeaders。
 * extraHeaders **不覆盖**已存在的键（厂商额外头只做补充）。
 */
QList<QPair<QString, QString>> requestHeaders(const ProviderConfig &config, bool stream);

/// 按 authStyle 生成鉴权头；query-key 风格返回空表（Key 走 URL）
QList<QPair<QString, QString>> authHeaders(const ProviderConfig &config);

/**
 * 把厂商原生参数片段合并进请求体：**跳过保留字段**（model / messages / stream / system），
 * 避免原生片段破坏协议必需结构。
 */
void mergeNativeParams(QJsonObject *body, const QJsonObject &nativeParams);

/// HTTP 状态码 → 三态校验结论：2xx Valid；401/403 Invalid；其余 Unverifiable
CredentialStatus credentialStatusForHttp(int httpStatus);

/// 按扩展名推断图片 MIME；未知时回退 "image/png"
QString guessMimeType(const QString &path);

/**
 * 读取图片原始字节：优先使用 image.bytes，其次读 image.path。
 * 失败（路径不存在 / 读取错误 / 无数据）返回 false 并写 error，绝不抛异常。
 * 同时输出生效的 MIME（image.mimeType 优先）。
 */
bool readImage(const ChatImage &image, QByteArray *bytes, QString *mimeType, QString *error);

/// 脱敏：仅保留前 4 位与长度提示，用于日志；空串返回 "<empty>"
QString maskSecret(const QString &secret);

}  // namespace ProviderUtils
