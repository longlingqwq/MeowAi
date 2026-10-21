#include "providers/OpenAiProvider.h"

#include "providers/ProviderUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>

namespace {

QString roleName(ChatMessage::Role role)
{
    switch (role) {
    case ChatMessage::System:
        return QStringLiteral("system");
    case ChatMessage::Assistant:
        return QStringLiteral("assistant");
    case ChatMessage::Tool:
        return QStringLiteral("tool");
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

/// 校验请求的失败分类 → 三态结论（**限流 / 断网一律不得判成「无效」**）
CredentialStatus statusForFailure(int httpStatus, NetworkTransport::Failure failure)
{
    switch (failure) {
    case NetworkTransport::Failure::Timeout:
    case NetworkTransport::Failure::NetworkUnavailable:
        return CredentialStatus::Unverifiable;
    case NetworkTransport::Failure::Cancelled:
        return CredentialStatus::Unknown;
    case NetworkTransport::Failure::Other:
        break;
    }
    return ProviderUtils::credentialStatusForHttp(httpStatus);
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

        // 工具结果回填：必须带 tool_call_id，content 即结果文本
        if (message.role == ChatMessage::Tool) {
            messageObject.insert(QStringLiteral("tool_call_id"), message.toolCallId);
            messageObject.insert(QStringLiteral("content"), message.text);
            messages.append(messageObject);
            continue;
        }

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

        // assistant 发起工具调用的那一轮：透传 tool_calls（与 content 并存）
        if (message.role == ChatMessage::Assistant && !message.toolCalls.isEmpty()) {
            QJsonArray toolCalls;
            for (const ChatToolCall &call : message.toolCalls) {
                QJsonObject function;
                function.insert(QStringLiteral("name"), call.name);
                function.insert(QStringLiteral("arguments"), call.arguments);
                QJsonObject callObject;
                callObject.insert(QStringLiteral("id"), call.id);
                callObject.insert(QStringLiteral("type"), QStringLiteral("function"));
                callObject.insert(QStringLiteral("function"), function);
                toolCalls.append(callObject);
            }
            messageObject.insert(QStringLiteral("tool_calls"), toolCalls);
        }

        messages.append(messageObject);
    }

    body->insert(QStringLiteral("model"), request.model);
    body->insert(QStringLiteral("messages"), messages);
    body->insert(QStringLiteral("temperature"), request.temperature);
    if (request.maxTokens > 0)
        body->insert(QStringLiteral("max_tokens"), request.maxTokens);
    body->insert(QStringLiteral("stream"), request.stream);
    // 工具清单（function calling）；为空表示普通对话，不下发该字段
    if (!request.tools.isEmpty()) {
        body->insert(QStringLiteral("tools"), request.tools);
        if (!request.toolChoice.isEmpty())
            body->insert(QStringLiteral("tool_choice"), request.toolChoice);
    }
    // 思考档位 / 高级原生参数：由 catalog 映射得到，此处只做透传（不覆盖核心字段）
    ProviderUtils::mergeNativeParams(body, request.nativeParams);
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
    m_toolCalls.clear();
    m_finishReason = ChatResponse::NotSpecified;

    NetworkTransport::Request transportRequest;
    transportRequest.url = ProviderUtils::endpointUrl(m_config, Protocol::OpenAI,
                                                      QStringLiteral("/chat/completions"));
    transportRequest.headers = ProviderUtils::requestHeaders(m_config, m_streaming);
    transportRequest.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    transportRequest.timeoutMs = 60000;

    m_handle = m_transport->post(transportRequest);
    if (m_handle == 0)
        fail(ChatError::Unknown, 0, QStringLiteral("无法发起网络请求"));
}

NetworkTransport::Request OpenAiProvider::auxRequest(const QString &path, const QString &method) const
{
    NetworkTransport::Request request;
    request.url = ProviderUtils::endpointUrl(m_config, Protocol::OpenAI, path);
    request.method = method;
    request.headers = ProviderUtils::requestHeaders(m_config, false);
    request.timeoutMs = 20000;
    return request;
}

void OpenAiProvider::validateCredentials()
{
    if (!isConfigured()) {
        finishValidation(CredentialStatus::NotConfigured, QStringLiteral("未配置 API Key"));
        return;
    }
    if (m_validateHandle != 0) {
        m_transport->abort(m_validateHandle);
        m_validateHandle = 0;
    }

    m_auxBody.clear();
    // validation.method == "models" 的厂商走 `GET /models`；其余走一次最小对话请求
    if (m_config.validationMethod == QStringLiteral("models")) {
        m_auxIsGet = true;
        m_validateHandle = m_transport->get(auxRequest(QStringLiteral("/models"), QStringLiteral("GET")));
    } else {
        m_auxIsGet = false;
        QJsonObject body;
        body.insert(QStringLiteral("model"), m_config.model);
        body.insert(QStringLiteral("stream"), false);
        body.insert(QStringLiteral("max_tokens"), 1);
        QJsonArray messages;
        QJsonObject message;
        message.insert(QStringLiteral("role"), QStringLiteral("user"));
        message.insert(QStringLiteral("content"), QStringLiteral("ping"));
        messages.append(message);
        body.insert(QStringLiteral("messages"), messages);

        NetworkTransport::Request request =
            auxRequest(QStringLiteral("/chat/completions"), QStringLiteral("POST"));
        request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
        m_validateHandle = m_transport->post(request);
    }

    if (m_validateHandle == 0)
        finishValidation(CredentialStatus::Unverifiable, QStringLiteral("无法发起校验请求"));
}

void OpenAiProvider::fetchModels()
{
    if (m_config.validationMethod != QStringLiteral("models")) {
        emit modelsFetched(QStringList(), QStringLiteral("该厂商不支持动态拉取模型清单"));
        return;
    }
    if (!isConfigured()) {
        emit modelsFetched(QStringList(), QStringLiteral("未配置 API Key"));
        return;
    }
    if (m_modelsHandle != 0) {
        m_transport->abort(m_modelsHandle);
        m_modelsHandle = 0;
    }

    m_auxBody.clear();
    m_modelsHandle = m_transport->get(auxRequest(QStringLiteral("/models"), QStringLiteral("GET")));
    if (m_modelsHandle == 0)
        emit modelsFetched(QStringList(), QStringLiteral("无法发起拉取请求"));
}

void OpenAiProvider::finishValidation(CredentialStatus status, const QString &message)
{
    emit credentialsChecked(status, message);
}

QStringList OpenAiProvider::parseModelIds() const
{
    QStringList ids;
    m_modelContextWindows.clear();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(m_auxBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return ids;

    // OpenAI 兼容厂商统一返回 {"data":[{"id":"..."}]}；
    // OpenRouter / Together / Groq / DeepInfra 等还会附带上下文窗口字段，能读到就用
    static const QStringList kWindowKeys = {
        QStringLiteral("context_length"),
        QStringLiteral("context_window"),
        QStringLiteral("max_context_tokens"),
        QStringLiteral("max_input_tokens"),
    };

    const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue &entry : data) {
        const QJsonObject object = entry.toObject();
        const QString id = object.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || ids.contains(id))
            continue;
        ids.append(id);

        for (const QString &key : kWindowKeys) {
            const int window = object.value(key).toInt(0);
            if (window > 0) {
                m_modelContextWindows.insert(id, window);
                break;
            }
        }
    }
    return ids;
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
    for (const QJsonValue &choiceValue : choices) {
        const QJsonObject choice = choiceValue.toObject();
        const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
        const QString piece = delta.value(QStringLiteral("content")).toString();
        if (!piece.isEmpty()) {
            m_accumulated += piece;
            emit chatDelta(piece);
        }
        // 流式工具调用：delta.tool_calls 的每个分片按 index 拼接（name 整段到达、arguments 逐段追加）
        const QJsonArray toolCalls = delta.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &callValue : toolCalls)
            accumulateToolCall(callValue.toObject());
        // 收尾原因（通常在最后一个分片出现，此时 content 为 null）
        const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString();
        if (!finishReason.isEmpty())
            m_finishReason = finishReasonFrom(finishReason);
    }

    const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
    if (!usage.isEmpty())
        m_usage = parseUsage(usage);
    return true;
}

void OpenAiProvider::accumulateToolCall(const QJsonObject &object)
{
    if (object.isEmpty())
        return;
    // 流式分片带 index；非流式整包不带 index，统一落在 0
    const int index = object.contains(QStringLiteral("index"))
                          ? object.value(QStringLiteral("index")).toInt()
                          : 0;
    ChatToolCall &call = m_toolCalls[index];

    const QString id = object.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        call.id = id;

    const QJsonObject function = object.value(QStringLiteral("function")).toObject();
    const QString name = function.value(QStringLiteral("name")).toString();
    if (!name.isEmpty())
        call.name = name;
    // arguments 为逐段增量，必须**追加**拼接
    call.arguments += function.value(QStringLiteral("arguments")).toString();
}

ChatResponse::FinishReason OpenAiProvider::finishReasonFrom(const QString &value)
{
    if (value == QStringLiteral("stop"))
        return ChatResponse::Stop;
    if (value == QStringLiteral("tool_calls") || value == QStringLiteral("function_call"))
        return ChatResponse::ToolCalls;
    if (value == QStringLiteral("length"))
        return ChatResponse::Length;
    return ChatResponse::Other;
}

QList<ChatToolCall> OpenAiProvider::collectToolCalls() const
{
    QList<int> indexes = m_toolCalls.keys();
    std::sort(indexes.begin(), indexes.end());
    QList<ChatToolCall> calls;
    calls.reserve(indexes.size());
    for (int index : indexes) {
        const ChatToolCall call = m_toolCalls.value(index);
        if (call.isValid())
            calls.append(call);
    }
    return calls;
}

void OpenAiProvider::handleChunk(int handle, const QByteArray &chunk)
{
    // 校验 / 模型清单请求：只累积响应体，不做协议解析
    if (handle == m_validateHandle || handle == m_modelsHandle) {
        m_auxBody.append(chunk);
        return;
    }
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
    if (handle == m_validateHandle) {
        m_validateHandle = 0;
        finishValidation(CredentialStatus::Valid, QStringLiteral("Key 有效"));
        return;
    }
    if (handle == m_modelsHandle) {
        m_modelsHandle = 0;
        const QStringList ids = parseModelIds();
        if (ids.isEmpty())
            emit modelsFetched(QStringList(), QStringLiteral("未能从厂商接口解析出模型清单"));
        else
            emit modelsFetched(ids, QString());
        return;
    }
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
        if (m_accumulated.isEmpty() && m_toolCalls.isEmpty()) {
            fail(ChatError::EmptyResponse, 200, QStringLiteral("流式响应为空"));
            return;
        }
        response.text = m_accumulated;
        response.toolCalls = collectToolCalls();
        response.finishReason = response.toolCalls.isEmpty()
                                    ? (m_finishReason == ChatResponse::NotSpecified ? ChatResponse::Stop
                                                                                    : m_finishReason)
                                    : ChatResponse::ToolCalls;
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
        const QJsonObject firstChoice = choices.first().toObject();
        const QJsonObject message = firstChoice.value(QStringLiteral("message")).toObject();
        response.text = message.value(QStringLiteral("content")).toString();
        for (const QJsonValue &callValue : message.value(QStringLiteral("tool_calls")).toArray())
            accumulateToolCall(callValue.toObject());
        response.toolCalls = collectToolCalls();
        if (response.text.isEmpty() && response.toolCalls.isEmpty()) {
            fail(ChatError::EmptyResponse, 200, QStringLiteral("响应内容为空"));
            return;
        }
        const QString finishReason = firstChoice.value(QStringLiteral("finish_reason")).toString();
        response.finishReason = response.toolCalls.isEmpty()
                                    ? (finishReason.isEmpty() ? ChatResponse::Stop
                                                              : finishReasonFrom(finishReason))
                                    : ChatResponse::ToolCalls;
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
    if (handle == m_validateHandle) {
        m_validateHandle = 0;
        const CredentialStatus status = statusForFailure(httpStatus, failure);
        finishValidation(status, status == CredentialStatus::Invalid
                                      ? QStringLiteral("Key 无效或无权限")
                                      : QStringLiteral("无法验证（限流或网络问题）"));
        return;
    }
    if (handle == m_modelsHandle) {
        m_modelsHandle = 0;
        emit modelsFetched(QStringList(), QStringLiteral("拉取失败，已保留内置清单"));
        return;
    }
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
