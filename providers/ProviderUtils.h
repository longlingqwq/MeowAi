#pragma once

#include "providers/AiProvider.h"
#include "providers/ChatTypes.h"

#include <QByteArray>
#include <QString>

/**
 * Provider 公共工具：默认域名、图片读取 / 编码、日志脱敏。
 * 不含任何厂商专属协议细节，供 OpenAiProvider / AnthropicProvider 共用。
 */
namespace ProviderUtils {

/// 厂商默认 API 根地址（不含具体端点路径）
QString defaultBaseUrl(AiProvider::Protocol protocol);

/// 解析生效的 API 根地址：config.baseUrl 优先，末尾 '/' 会被去除
QString resolveBaseUrl(const ProviderConfig &config, AiProvider::Protocol protocol);

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
