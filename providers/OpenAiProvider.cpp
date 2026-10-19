#include "providers/OpenAiProvider.h"

#include "providers/ProviderUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

namespace {

QString roleName(ChatMessage::Role role)
{
    switch (role) {
    case ChatMessage::System:
        return QStringLiteral("system");
    case ChatMessage::Assistant:
        return QStringLiteral("assistant");
    case ChatMessage::User:
        break;
    }
    return QStringLiteral("user");
}

bool requestHasImages(const ChatRequest &request)
{
    for (const ChatMessage &message : request.messages) {
        if (!message.images.isEmpty())
            return true;
    }
    return false;
}

ChatUsage parseUsage(const QJsonObject &usage)
{
    ChatUsage result;
    if (usage.contains(QStringLiteral("prompt_tokens")))
        result.promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt(-1);
    if (usage.contains(QStringLiteral("completion_tokens")))
        result.completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt(-1);
    if (usage.contains(QStringLiteral("total_tokens")))
        result.totalTokens = usage.value(QStringLiteral("total_tokens")).toInt(-1);
    return result;
}

/// 从 {"error":{...}} 中提取可读信息；不含敏感数据
QString errorText(const QJsonObject &object)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    QString message = error.value(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = error.value(QStringLiteral("type")).toString();
    if (message.isEmpty())
        message = QStringLiteral("服务端返回错误");
    return message;
}

}  // namespace

OpenAiProvider::OpenAiProvider(NetworkTransport *transport, QObject *parent)
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

AiProvider::Protocol OpenAiProvider::protocol() const
{
    return Protocol::OpenAI;
}

QString OpenAiProvider::providerId() const
{
    return QStringLiteral("openai");
}

bool OpenAiProvider::isConfigured() const
{
    return !m_config.apiKey.isEmpty();
}

void OpenAiProvider::configure(const ProviderConfig &config)
{
    m_config = config;
}

bool OpenAiProvider::buildBody(const ChatRequest &request, QJsonObject *body, QString *error) const
{
    QJsonArray messages;
    for (const ChatMessage &message : request.messages) {
        QJsonObject messageObject;
        messageObject.insert(QStringLiteral("role"), roleName(message.role));

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
                QJsonObject urlObject;
                urlObject.insert(QStringLiteral("url"),
                                 QStringLiteral("data:%1;base64,%2")
                                     .arg(mime, QString::fromLatin1(bytes.toBase64())));
                QJsonObject imageBlock;
                imageBlock.insert(QStringLiteral("type"), QStringLiteral("image_url"));
                imageBlock.insert(QStringLiteral("image_url"), urlObject);
                blocks.append(imageBlock);
            }
            messageObject.insert(QStringLiteral("content"), blocks);
        }
        messages.append(messageObject);
    }

    body->insert(QStringLiteral("model"), request.model);
    body->insert(QStringLiteral("messages"), messages);
    body->insert(QStringLiteral("temperature"), request.temperature);
    if (request.maxTokens > 0)
        body->insert(QStringLiteral("max_tokens"), request.maxTokens);
    body->insert(QStringLiteral("stream"), request.stream);
    return true;
}

void OpenAiProvider::sendChat(const ChatRequest &request)
{
    if (!isConfigured()) {
        fail(ChatError::NotConfigured, 0, QStringLiteral("未配置 OpenAI API Key"));
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
    m_sse.reset();

    NetworkTransport::Request transportRequest;
    transportRequest.url = ProviderUtils::resolveBaseUrl(m_config, Protocol::OpenAI)
                           + QStringLiteral("/chat/completions");
    transportRequest.headers = {
        {QStringLiteral("Content-Type"), QStringLiteral("application/json")},
        {QStringLiteral("Accept"), m_streaming ? QStringLiteral("text/event-stream")
                                               : QStringLiteral("application/json")},
        {QStringLiteral("Authorization"), QStringLiteral("Bearer ") + m_config.apiKey},
    };
    transportRequest.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    transportRequest.timeoutMs = 60000;

    m_handle = m_transport->post(transportRequest);
    if (m_handle == 0)
        fail(ChatError::Unknown, 0, QStringLiteral("无法发起网络请求"));
}

bool OpenAiProvider::processStreamEvent(const SseEvent &event)
{
    const QByteArray data = event.data.trimmed();
    if (data.isEmpty())
        return true;  // 心跳 / 注释
    if (data == "[DONE]")
        return true;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return true;  // 容错：跳过无法解析的分片，不因单条脏数据中断

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("error"))) {
        abortActive();
        fail(ChatError::Unknown, 0, errorText(object));
        return false;
    }

    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    for (const QJsonValue &choice : choices) {
        const QJsonObject delta = choice.toObject().value(QStringLiteral("delta")).toObject();
        const QString piece = delta.value(QStringLiteral("content")).toString();
        if (!piece.isEmpty()) {
            m_accumulated += piece;
            emit chatDelta(piece);
        }
    }

    const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
    if (!usage.isEmpty())
        m_usage = parseUsage(usage);
    return true;
}

void OpenAiProvider::handleChunk(int handle, const QByteArray &chunk)
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

void OpenAiProvider::handleFinished(int handle)
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
        if (object.contains(QStringLiteral("error"))) {
            fail(ChatError::Unknown, 0, errorText(object));
            return;
        }
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            fail(ChatError::EmptyResponse, 200, QStringLiteral("响应不含任何候选结果"));
            return;
        }
        response.text = choices.first().toObject()
                            .value(QStringLiteral("message"))
                            .toObject()
                            .value(QStringLiteral("content"))
                            .toString();
        if (response.text.isEmpty()) {
            fail(ChatError::EmptyResponse, 200, QStringLiteral("响应内容为空"));
            return;
        }
        m_usage = parseUsage(object.value(QStringLiteral("usage")).toObject());
        response.model = object.value(QStringLiteral("model")).toString();
    }

    response.ok = true;
    response.usage = m_usage;
    if (response.model.isEmpty())
        response.model = m_config.model;
    response.rawJson = QString::fromUtf8(m_streaming ? QByteArray() : m_rawBody);
    emit chatFinished(response);
}

void OpenAiProvider::handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure,
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

void OpenAiProvider::fail(ChatError::Code code, int httpStatus, const QString &message)
{
    emit chatFailed(ChatError{code, httpStatus, message});
}

void OpenAiProvider::abortActive()
{
    if (m_handle == 0)
        return;
    const int handle = m_handle;
    m_handle = 0;
    m_transport->abort(handle);
}

void OpenAiProvider::cancel()
{
    if (m_handle == 0)
        return;
    abortActive();
    emit chatFailed(ChatError{ChatError::Cancelled, 0, QStringLiteral("请求已取消")});
}
