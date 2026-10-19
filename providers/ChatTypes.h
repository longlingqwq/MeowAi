#pragma once

#include <QByteArray>
#include <QList>
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
    QString mimeType;  ///< 如 "image/png"
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
};

struct ChatResponse
{
    bool ok = false;
    QString text;
    QString error;
    QString rawJson;
};

/// Provider 连接配置；apiKey 由 storage/SecureStore 提供，禁止明文落盘
struct ProviderConfig
{
    QString apiKey;
    QString baseUrl;  ///< 为空则使用厂商默认域名
    QString model;
};
