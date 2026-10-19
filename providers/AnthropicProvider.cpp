#include "providers/AnthropicProvider.h"

#include "providers/ProviderUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

namespace {

bool requestHasImages(const ChatRequest &request)
{
    for (const ChatMessage &message : request.messages) {
        if (!message.images.isEmpty())
            return true;
    }
    return false;
}

QString errorText(const QJsonObject &object)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    QString message = error.value(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = error.value(QStringLiteral("type")).toString();
    if (message.isEmpty())
        message = object.value(QStringLiteral("type")).toString();
    if (message.isEmpty())
        message = QStringLiteral("服务端返回错误");
    return message;
}

}  // namespace

AnthropicProvider::AnthropicProvider(NetworkTransport *transport, QObject *parent)
    : AiProvider(parent)
{
    m_transport = transport ? transport : new QNetworkTransport(this);

    connect(m_transport, &NetworkTransport::chunkReceived, this,
            [this](int handle, const QByteArray &chunk) { handleChunk(handle, chunk); });
    connect(m_transport, &NetworkTransport::finished, this, [this](int handle) { handleFinished(handle); });
    connect(m_transport, &NetworkTransport::failed, this,
            [this](int handle, int status, NetworkTransport::Failure failure, const QString &text) {
                handleFailed(handle, status, failure, text);
            });
}

AiProvider::Protocol AnthropicProvider::protocol() const
{
    return Protocol::Anthropic;
}

QString AnthropicProvider::providerId() const
{
    return QStringLiteral("anthropic");
}

bool AnthropicProvider::isConfigured() const
{
    return !m_config.apiKey.isEmpty();
}

void AnthropicProvider::configure(const ProviderConfig &config)
{
    m_config = config;
}

bool AnthropicProvider::buildBody(const ChatRequest &request, QJsonObject *body, QString *error) const
{
    // system 为 Anthropic 顶层字段，需从消息列表中分离
    QStringList systemParts;
    QJsonArray messages;
    for (const ChatMessage &message : request.messages) {
        if (message.role == ChatMessage::System) {
            if (!message.text.isEmpty())
                systemParts.append(message.text);
            continue;
        }

        QJsonObject messageObject;
        messageObject.insert(QStringLiteral("role"),
                             message.role == ChatMessage::Assistant ? QStringLiteral("assistant")
                                                                    : QStringLiteral("user"));

        if (message.images.isEmpty()) {
            messageObject.insert(QStringLiteral("content"), message.text);
        } else {
            QJsonArray blocks;
            if (!message.text.isEmpty()) {
                QJsonObject textBlock;
                textBlock.insert(QStringLiteral("type"), QStringLiteral("text"));
                textBlock.insert(QStringLiteral("text"), message.text);
                blocks.append(textBlock);
            }
            for (const ChatImage &image : message.images) {
                QByteArray bytes;
                QString mime;
                QString imageError;
                if (!ProviderUtils::readImage(image, &bytes, &mime, &imageError)) {
                    if (error)
                        *error = imageError;
                    return false;
                }
                QJsonObject source;
                source.insert(QStringLiteral("type"), QStringLiteral("base64"));
                source.insert(QStringLiteral("media_type"), mime);
                source.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
                QJsonObject imageBlock;
                imageBlock.insert(QStringLiteral("type"), QStringLiteral("image"));
                imageBlock.insert(QStringLiteral("source"), source);
                blocks.append(imageBlock);
            }
            messageObject.insert(QStringLiteral("content"), blocks);
        }
        messages.append(messageObject);
    }

    if (!systemParts.isEmpty())
        body->insert(QStringLiteral("system"), systemParts.join(QStringLiteral("\n\n")));
    body->insert(QStringLiteral("model"), request.model);
    body->insert(QStringLiteral("messages"), messages);
    // Anthropic 要求 max_tokens 必填；未指定时给出保守默认值
    body->insert(QStringLiteral("max_tokens"), request.maxTokens > 0 ? request.maxTokens : 1024);
    body->insert(QStringLiteral("temperature"), request.temperature);
    body->insert(QStringLiteral("stream"), request.stream);
    return true;
}

void AnthropicProvider::sendChat(const ChatRequest &request)
{
    if (!isConfigured()) {
        fail(ChatError::NotConfigured, 0, QStringLiteral("未配置 Anthropic API Key"));
        return;
    }
    if (requestHasImages(request) && !request.imagesConfirmed) {
        fail(ChatError::ImagesNotConfirmed, 0,
             QStringLiteral("请求包含图片，需用户确认后才能发送"));
        return;
    }

    if (m_handle != 0)
        abortActive();

    QJsonObject body;
    QString buildError;
    if (!buildBody(request, &body, &buildError)) {
        fail(ChatError::Unknown, 0, QStringLiteral("图片读取失败：%1").arg(buildError));
        return;
    }

    m_streaming = request.stream;
    m_accumulated.clear();
    m_rawBody.clear();
    m_usage = ChatUsage{};
    m_model.clear();
    m_sse.reset();

    NetworkTransport::Request transportRequest;
    transportRequest.url = ProviderUtils::resolveBaseUrl(m_config, Protocol::Anthropic)
                           + QStringLiteral("/messages");
    transportRequest.headers = {
        {QStringLiteral("Content-Type"), QStringLiteral("application/json")},
        {QStringLiteral("Accept"), m_streaming ? QStringLiteral("text/event-stream")
                                               : QStringLiteral("application/json")},
        {QStringLiteral("x-api-key"), m_config.apiKey},
        {QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01")},
    };
    transportRequest.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    transportRequest.timeoutMs = 60000;

    m_handle = m_transport->post(transportRequest);
    if (m_handle == 0)
        fail(ChatError::Unknown, 0, QStringLiteral("无法发起网络请求"));
}

bool AnthropicProvider::processStreamEvent(const SseEvent &event)
{
    const QByteArray data = event.data.trimmed();
    if (data.isEmpty())
        return true;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return true;

    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("error")) {
        abortActive();
        fail(ChatError::Unknown, 0, errorText(object));
        return false;
    }
    if (type == QStringLiteral("content_block_delta")) {
        const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
        const QString piece = delta.value(QStringLiteral("text")).toString();
        if (!piece.isEmpty()) {
            m_accumulated += piece;
            emit chatDelta(piece);
        }
    } else if (type == QStringLiteral("message_start")) {
        const QJsonObject message = object.value(QStringLiteral("message")).toObject();
        m_model = message.value(QStringLiteral("model")).toString(m_model);
        const QJsonObject usage = message.value(QStringLiteral("usage")).toObject();
        if (usage.contains(QStringLiteral("input_tokens")))
            m_usage.promptTokens = usage.value(QStringLiteral("input_tokens")).toInt(-1);
    } else if (type == QStringLiteral("message_delta")) {
        const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
        if (usage.contains(QStringLiteral("output_tokens")))
            m_usage.completionTokens = usage.value(QStringLiteral("output_tokens")).toInt(-1);
    }
    return true;
}

void AnthropicProvider::handleChunk(int handle, const QByteArray &chunk)
{
    if (handle != m_handle || m_handle == 0)
        return;

    if (!m_streaming) {
        m_rawBody.append(chunk);
        return;
    }

    const QVector<SseEvent> events = m_sse.feed(chunk);
    for (const SseEvent &event : events) {
        if (!processStreamEvent(event))
            return;
    }
}

void AnthropicProvider::handleFinished(int handle)
{
    if (handle != m_handle)
        return;
    m_handle = 0;

    ChatResponse response;

    if (m_streaming) {
        const QVector<SseEvent> tail = m_sse.finish();
        for (const SseEvent &event : tail) {
            if (!processStreamEvent(event))
                return;
        }
        if (m_accumulated.isEmpty()) {
            fail(ChatError::EmptyResponse, 200, QStringLiteral("流式响应为空"));
            return;
        }
        response.text = m_accumulated;
    } else {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_rawBody, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(ChatError::InvalidResponse, 200, QStringLiteral("响应无法解析"));
            return;
        }
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("error")) {
            fail(ChatError::Unknown, 0, errorText(object));
            return;
        }
        const QJsonArray content = object.value(QStringLiteral("content")).toArray();
        QString text;
        for (const QJsonValue &block : content) {
            const QJsonObject blockObject = block.toObject();
            if (blockObject.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
                text += blockObject.value(QStringLiteral("text")).toString();
        }
        if (text.isEmpty()) {
            fail(ChatError::EmptyResponse, 200, QStringLiteral("响应内容为空"));
            return;
        }
        response.text = text;

        const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
        if (usage.contains(QStringLiteral("input_tokens")))
            m_usage.promptTokens = usage.value(QStringLiteral("input_tokens")).toInt(-1);
        if (usage.contains(QStringLiteral("output_tokens")))
            m_usage.completionTokens = usage.value(QStringLiteral("output_tokens")).toInt(-1);
        m_model = object.value(QStringLiteral("model")).toString(m_model);
    }

    if (m_usage.promptTokens >= 0 && m_usage.completionTokens >= 0)
        m_usage.totalTokens = m_usage.promptTokens + m_usage.completionTokens;

    response.ok = true;
    response.usage = m_usage;
    response.model = m_model.isEmpty() ? m_config.model : m_model;
    response.rawJson = QString::fromUtf8(m_streaming ? QByteArray() : m_rawBody);
    emit chatFinished(response);
}

void AnthropicProvider::handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure,
                                     const QString &errorText)
{
    if (handle != m_handle)
        return;
    m_handle = 0;

    switch (failure) {
    case NetworkTransport::Failure::Cancelled:
        emit chatFailed(ChatError{ChatError::Cancelled, httpStatus, QStringLiteral("请求已取消")});
        return;
    case NetworkTransport::Failure::Timeout:
        emit chatFailed(ChatError{ChatError::Timeout, httpStatus, QStringLiteral("请求超时")});
        return;
    case NetworkTransport::Failure::NetworkUnavailable:
        emit chatFailed(ChatError{ChatError::NetworkUnavailable, httpStatus,
                                  QStringLiteral("网络不可用：") + errorText});
        return;
    case NetworkTransport::Failure::Other:
        break;
    }

    if (httpStatus == 401 || httpStatus == 403)
        fail(ChatError::Unauthorized, httpStatus, QStringLiteral("API Key 无效或无权限"));
    else if (httpStatus == 429)
        fail(ChatError::RateLimited, httpStatus, QStringLiteral("请求过于频繁（限流）"));
    else if (httpStatus >= 500)
        fail(ChatError::ServerError, httpStatus, QStringLiteral("服务端错误"));
    else if (httpStatus > 0)
        fail(ChatError::HttpError, httpStatus, QStringLiteral("HTTP 错误 %1").arg(httpStatus));
    else
        fail(ChatError::Unknown, 0, errorText.isEmpty() ? QStringLiteral("请求失败") : errorText);
}

void AnthropicProvider::fail(ChatError::Code code, int httpStatus, const QString &message)
{
    emit chatFailed(ChatError{code, httpStatus, message});
}

void AnthropicProvider::abortActive()
{
    if (m_handle == 0)
        return;
    const int handle = m_handle;
    m_handle = 0;
    m_transport->abort(handle);
}

void AnthropicProvider::cancel()
{
    if (m_handle == 0)
        return;
    abortActive();
    emit chatFailed(ChatError{ChatError::Cancelled, 0, QStringLiteral("请求已取消")});
}
