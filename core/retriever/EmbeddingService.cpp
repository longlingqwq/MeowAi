#include "core/retriever/EmbeddingService.h"

#include "providers/AiProvider.h"
#include "providers/NetworkTransport.h"
#include "providers/ProviderUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

QString sanitizeMessage(const QString &message)
{
    QString text = message.simplified();
    if (text.size() > 200) {
        text = text.left(200) + QStringLiteral("…");
    }
    return text;
}

/// 从 HTTP 错误响应里提取可读原因（OpenAI 兼容格式 {"error":{"message":...}}）。
/// 取不到结构化字段时退回原始文本，已经是「为什么失败」的唯一线索，不能丢。
QString serverMessage(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return QString();
    }
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (document.isObject()) {
        const QJsonObject detail =
            document.object().value(QStringLiteral("error")).toObject();
        const QString message = detail.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return sanitizeMessage(message);
        }
        const QString code = detail.value(QStringLiteral("code")).toString();
        if (!code.isEmpty()) {
            return sanitizeMessage(code);
        }
    }
    return sanitizeMessage(text);
}

/// 未就绪原因；空串表示就绪。**不包含任何密钥**
QString notReadyReason(const ProviderConfig &provider, const LibraryEmbeddingConfig &embedding)
{
    if (provider.providerId.trimmed() == QStringLiteral("anthropic")) {
        return QStringLiteral("Anthropic 未提供 embedding 接口，请单独配置一个支持 /embeddings 的服务");
    }
    if (provider.apiKey.trimmed().isEmpty()) {
        return QStringLiteral("未配置 embedding 服务的 API Key，已标记为待索引");
    }
    if (embedding.modelId.trimmed().isEmpty()) {
        return QStringLiteral("未指定 embedding 模型，已标记为待索引");
    }
    if (provider.baseUrl.trimmed().isEmpty()) {
        return QStringLiteral("未填写 embedding 服务地址，已标记为待索引");
    }
    return QString();
}

int addUsageField(int current, int value)
{
    if (value < 0) {
        return current;
    }
    return current < 0 ? value : current + value;
}

ChatUsage parseUsage(const QJsonObject &usage)
{
    ChatUsage result;
    if (usage.contains(QStringLiteral("prompt_tokens"))) {
        result.promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt(-1);
    }
    if (usage.contains(QStringLiteral("total_tokens"))) {
        result.totalTokens = usage.value(QStringLiteral("total_tokens")).toInt(-1);
    }
    if (result.totalTokens < 0 && result.promptTokens >= 0) {
        result.totalTokens = result.promptTokens;
    }
    return result;
}

/// 把 NetworkTransport 的失败分类折叠成结构化错误码，供中文原因映射使用
ChatError::Code codeForFailure(int httpStatus, NetworkTransport::Failure failure)
{
    switch (failure) {
    case NetworkTransport::Failure::Cancelled:
        return ChatError::Cancelled;
    case NetworkTransport::Failure::Timeout:
        return ChatError::Timeout;
    case NetworkTransport::Failure::NetworkUnavailable:
        return ChatError::NetworkUnavailable;
    case NetworkTransport::Failure::Other:
        break;
    }
    if (httpStatus == 401 || httpStatus == 403)
        return ChatError::Unauthorized;
    if (httpStatus == 429)
        return ChatError::RateLimited;
    if (httpStatus >= 500)
        return ChatError::ServerError;
    if (httpStatus > 0)
        return ChatError::HttpError;
    return ChatError::Unknown;
}

/// `GET /models` 失败原因：必须比「拉取失败」更具体，且不包含响应正文
QString modelFetchReason(int httpStatus, NetworkTransport::Failure failure)
{
    if (httpStatus == 401 || httpStatus == 403)
        return QStringLiteral("密钥无效或无权限");
    if (httpStatus == 404)
        return QStringLiteral("该服务未提供 /models 接口");
    if (httpStatus == 429)
        return QStringLiteral("触发限流，请稍后重试");
    if (failure == NetworkTransport::Failure::Timeout
        || failure == NetworkTransport::Failure::NetworkUnavailable
        || httpStatus >= 500 || httpStatus <= 0) {
        return QStringLiteral("无法判定，请检查网络或稍后重试");
    }
    return QStringLiteral("拉取失败（HTTP %1）").arg(httpStatus);
}

QStringList parseModelIds(const QByteArray &body)
{
    QStringList ids;
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return ids;
    const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue &value : data) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

}  // namespace

EmbeddingService::EmbeddingService(NetworkTransport *transport, QObject *parent)
    : QObject(parent)
{
    m_transport = transport ? transport : new QNetworkTransport(this);

    connect(m_transport, &NetworkTransport::chunkReceived, this, [this](int handle, const QByteArray &chunk) {
        if (handle == m_handle) {
            m_body.append(chunk);
        } else if (handle == m_validationHandle) {
            m_validationBody.append(chunk);
        } else if (handle == m_modelsHandle) {
            m_modelsBody.append(chunk);
        }
    });
    connect(m_transport, &NetworkTransport::finished, this,
            [this](int handle) { handleFinished(handle); });
    connect(m_transport, &NetworkTransport::failed, this,
            [this](int handle, int status, NetworkTransport::Failure failure, const QString &text) {
                handleFailed(handle, status, failure, text);
            });
}

void EmbeddingService::configure(const ProviderConfig &provider,
                                 const LibraryEmbeddingConfig &embedding)
{
    m_provider = provider;
    m_embedding = embedding;
}

ProviderConfig EmbeddingService::providerConfig() const
{
    return m_provider;
}

LibraryEmbeddingConfig EmbeddingService::embeddingConfig() const
{
    return m_embedding;
}

bool EmbeddingService::isReady() const
{
    return notReadyReason(m_provider, m_embedding).isEmpty();
}

bool EmbeddingService::busy() const
{
    return m_busy;
}

void EmbeddingService::embed(const QStringList &texts)
{
    if (m_busy) {
        ChatError error;
        error.code = ChatError::Unknown;
        error.message = QStringLiteral("已有向量化请求进行中，请稍后再试");
        emit failed(error);
        return;
    }

    // 延迟索引：没有可用 embedding 服务时**一个请求都不发**，由上层标记待索引
    const QString reason = notReadyReason(m_provider, m_embedding);
    if (!reason.isEmpty()) {
        ChatError error;
        error.code = ChatError::NotConfigured;
        error.message = reason;
        emit failed(error);
        return;
    }

    if (texts.isEmpty()) {
        EmbeddingResult result;
        result.ok = true;
        emit finished(result);
        return;
    }

    m_inputs = texts;
    m_nextIndex = 0;
    m_vectors.clear();
    m_body.clear();
    m_usage = ChatUsage();
    m_dimensions = m_embedding.dimensions > 0 ? m_embedding.dimensions : 0;
    m_busy = true;

    issueNextBatch();
}

void EmbeddingService::cancel()
{
    if (!m_busy) {
        return;
    }
    if (m_handle != 0) {
        m_transport->abort(m_handle);
        m_handle = 0;
    }
    m_busy = false;
    m_inputs.clear();
    m_vectors.clear();
    m_body.clear();

    ChatError error;
    error.code = ChatError::Cancelled;
    error.message = QStringLiteral("向量化已取消");
    emit failed(error);
}

void EmbeddingService::issueNextBatch()
{
    const int batchSize = qMax(1, m_embedding.batchSize);
    const int remaining = m_inputs.size() - m_nextIndex;
    const int count = qMin(batchSize, remaining);

    QJsonArray input;
    for (int i = 0; i < count; ++i) {
        input.append(m_inputs.at(m_nextIndex + i));
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_embedding.modelId);
    body.insert(QStringLiteral("input"), input);
    if (m_embedding.dimensions > 0) {
        body.insert(QStringLiteral("dimensions"), m_embedding.dimensions);
    }

    NetworkTransport::Request request;
    request.url = ProviderUtils::endpointUrl(m_provider, AiProvider::Protocol::OpenAI,
                                              QStringLiteral("/embeddings"));
    request.method = QStringLiteral("POST");
    request.headers = ProviderUtils::requestHeaders(m_provider, false);
    request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    request.timeoutMs = 60000;

    m_body.clear();
    m_handle = m_transport->post(request);
    if (m_handle == 0) {
        ChatError error;
        error.code = ChatError::Unknown;
        error.message = QStringLiteral("无法发起 embedding 请求");
        fail(error);
    }
}

void EmbeddingService::handleFinished(int handle)
{
    if (handle == m_validationHandle) {
        m_validationHandle = 0;
        finishValidationSuccess(m_validationBody);
        m_validationBody.clear();
        return;
    }
    if (handle == m_modelsHandle) {
        m_modelsHandle = 0;
        finishModelsFetch(m_modelsBody);
        m_modelsBody.clear();
        return;
    }
    if (!m_busy || handle != m_handle) {
        return;
    }
    m_handle = 0;

    const int batchSize = qMax(1, m_embedding.batchSize);
    const int count = qMin(batchSize, m_inputs.size() - m_nextIndex);

    QString parseError;
    if (!parseBatch(m_body, count, &parseError)) {
        ChatError error;
        error.code = ChatError::InvalidResponse;
        error.httpStatus = 200;
        error.message = QStringLiteral("embedding 响应无法使用：%1").arg(parseError);
        fail(error);
        return;
    }

    m_nextIndex += count;
    if (m_nextIndex < m_inputs.size()) {
        issueNextBatch();
        return;
    }
    finishSuccess();
}

void EmbeddingService::handleFailed(int handle, int httpStatus,
                                    NetworkTransport::Failure failure, const QString &errorText)
{
    if (handle == m_validationHandle) {
        m_validationHandle = 0;
        m_validationBody.clear();
        failValidation(httpStatus, failure);
        return;
    }
    if (handle == m_modelsHandle) {
        m_modelsHandle = 0;
        m_modelsBody.clear();
        failModelsFetch(httpStatus, failure);
        return;
    }
    if (!m_busy || handle != m_handle) {
        return;
    }
    m_handle = 0;

    ChatError error;
    error.httpStatus = httpStatus;
    switch (failure) {
    case NetworkTransport::Failure::Cancelled:
        error.code = ChatError::Cancelled;
        error.message = QStringLiteral("向量化已取消");
        break;
    case NetworkTransport::Failure::Timeout:
        error.code = ChatError::Timeout;
        error.message = QStringLiteral("embedding 请求超时");
        break;
    case NetworkTransport::Failure::NetworkUnavailable:
        error.code = ChatError::NetworkUnavailable;
        error.message = QStringLiteral("网络不可用：") + sanitizeMessage(errorText);
        break;
    case NetworkTransport::Failure::Other:
        if (httpStatus == 401 || httpStatus == 403) {
            error.code = ChatError::Unauthorized;
            error.message = QStringLiteral("embedding 服务的 API Key 无效或无权限");
        } else if (httpStatus == 429) {
            error.code = ChatError::RateLimited;
            error.message = QStringLiteral("embedding 请求过于频繁（限流），不影响已入库片段");
        } else if (httpStatus >= 500) {
            error.code = ChatError::ServerError;
            error.message = QStringLiteral("embedding 服务端错误");
        } else if (httpStatus == 404) {
            error.code = ChatError::HttpError;
            error.message = QStringLiteral("embedding 端点不存在（请核对服务地址与模型）");
        } else if (httpStatus > 0) {
            error.code = ChatError::HttpError;
            // 带上服务端的原始原因（如「batch size 超过 10」），否则 400 无法自查
            const QString detail = serverMessage(errorText);
            error.message = detail.isEmpty()
                                ? QStringLiteral("embedding HTTP 错误 %1").arg(httpStatus)
                                : QStringLiteral("embedding HTTP 错误 %1：%2")
                                      .arg(httpStatus)
                                      .arg(detail);
        } else {
            error.code = ChatError::Unknown;
            error.message = errorText.isEmpty() ? QStringLiteral("embedding 请求失败")
                                                : sanitizeMessage(errorText);
        }
        break;
    }
    fail(error);
}

void EmbeddingService::validateConfig()
{
    // 校验期间禁止重复发起
    if (m_validationStatus == Checking)
        return;

    const QString reason = notReadyReason(m_provider, m_embedding);
    if (!reason.isEmpty()) {
        setValidation(Invalid, reason);
        return;
    }

    if (m_validationHandle != 0) {
        m_transport->abort(m_validationHandle);
        m_validationHandle = 0;
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_embedding.modelId);
    QJsonArray input;
    input.append(QStringLiteral("ping"));
    body.insert(QStringLiteral("input"), input);
    if (m_embedding.dimensions > 0)
        body.insert(QStringLiteral("dimensions"), m_embedding.dimensions);

    NetworkTransport::Request request;
    request.url = ProviderUtils::endpointUrl(m_provider, AiProvider::Protocol::OpenAI,
                                              QStringLiteral("/embeddings"));
    request.method = QStringLiteral("POST");
    request.headers = ProviderUtils::requestHeaders(m_provider, false);
    request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    request.timeoutMs = 20000;

    m_validationBody.clear();
    setValidation(Checking, QStringLiteral("正在校验 embedding 服务"));
    m_validationHandle = m_transport->post(request);
    if (m_validationHandle == 0)
        setValidation(Unverifiable, QStringLiteral("无法发起校验请求"));
}

void EmbeddingService::resetValidation()
{
    if (m_validationHandle != 0) {
        m_transport->abort(m_validationHandle);
        m_validationHandle = 0;
        m_validationBody.clear();
    }
    setValidation(Idle, QString());
}

QString EmbeddingService::validationStatus() const
{
    switch (m_validationStatus) {
    case Idle:
        return QStringLiteral("idle");
    case Checking:
        return QStringLiteral("checking");
    case Valid:
        return QStringLiteral("valid");
    case Invalid:
        return QStringLiteral("invalid");
    case Unverifiable:
        return QStringLiteral("unverifiable");
    }
    return QStringLiteral("idle");
}

QString EmbeddingService::validationMessage() const
{
    return m_validationMessage;
}

void EmbeddingService::setValidation(ValidationStatus status, const QString &message)
{
    if (m_validationStatus == status && m_validationMessage == message)
        return;
    m_validationStatus = status;
    m_validationMessage = message;
    emit validationChanged();
}

void EmbeddingService::finishValidationSuccess(const QByteArray &body)
{
    if (m_validationStatus != Checking)
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setValidation(Invalid, QStringLiteral("返回内容不是有效向量"));
        return;
    }

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("error"))) {
        setValidation(Invalid, QStringLiteral("服务端返回错误，请检查模型与地址"));
        return;
    }

    const QJsonArray data = object.value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        setValidation(Invalid, QStringLiteral("返回内容不是有效向量"));
        return;
    }
    const QJsonArray values = data.first().toObject().value(QStringLiteral("embedding")).toArray();
    if (values.isEmpty()) {
        setValidation(Invalid, QStringLiteral("返回内容不是有效向量"));
        return;
    }
    if (m_embedding.dimensions > 0 && values.size() != m_embedding.dimensions) {
        setValidation(Invalid, QStringLiteral("返回维度与配置不一致"));
        return;
    }

    setValidation(Valid, QStringLiteral("校验通过：向量维度 %1").arg(values.size()));
}

void EmbeddingService::failValidation(int httpStatus, NetworkTransport::Failure failure)
{
    if (m_validationStatus != Checking)
        return;

    const ChatError::Code code = codeForFailure(httpStatus, failure);
    // 429 / 超时 / 断网 / 5xx 只能判定为「无法判定」，不能误报配置无效
    const bool unverifiable = code == ChatError::RateLimited || code == ChatError::Timeout
                              || code == ChatError::NetworkUnavailable
                              || code == ChatError::ServerError || code == ChatError::Cancelled
                              || code == ChatError::Unknown;
    setValidation(unverifiable ? Unverifiable : Invalid,
                  validationFailureReason(httpStatus, code));
}

void EmbeddingService::fetchModels()
{
    if (m_provider.providerId.trimmed() == QStringLiteral("anthropic")) {
        emit modelsFetched(QStringList(),
                           QStringLiteral("Anthropic 未提供 embedding 接口，请单独配置一个支持 /embeddings 的服务"));
        return;
    }
    if (m_provider.apiKey.trimmed().isEmpty()) {
        emit modelsFetched(QStringList(), QStringLiteral("未配置 API Key"));
        return;
    }
    if (m_provider.baseUrl.trimmed().isEmpty()) {
        emit modelsFetched(QStringList(), QStringLiteral("未填写服务地址"));
        return;
    }
    if (m_modelsHandle != 0) {
        m_transport->abort(m_modelsHandle);
        m_modelsHandle = 0;
    }

    NetworkTransport::Request request;
    request.url = ProviderUtils::endpointUrl(m_provider, AiProvider::Protocol::OpenAI,
                                              QStringLiteral("/models"));
    request.method = QStringLiteral("GET");
    request.headers = ProviderUtils::requestHeaders(m_provider, false);
    request.timeoutMs = 20000;

    m_modelsBody.clear();
    m_modelsHandle = m_transport->get(request);
    if (m_modelsHandle == 0)
        emit modelsFetched(QStringList(), QStringLiteral("无法发起拉取请求"));
}

void EmbeddingService::finishModelsFetch(const QByteArray &body)
{
    const QStringList ids = parseModelIds(body);
    if (ids.isEmpty()) {
        emit modelsFetched(QStringList(), QStringLiteral("未能从厂商接口解析出模型清单"));
        return;
    }
    emit modelsFetched(ids, QString());
}

void EmbeddingService::failModelsFetch(int httpStatus, NetworkTransport::Failure failure)
{
    emit modelsFetched(QStringList(), modelFetchReason(httpStatus, failure));
}

bool EmbeddingService::parseBatch(const QByteArray &body, int expectedCount, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("响应不是合法 JSON");
        }
        return false;
    }

    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("error"))) {
        const QJsonObject detail = object.value(QStringLiteral("error")).toObject();
        QString message = detail.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) {
            message = detail.value(QStringLiteral("code")).toString();
        }
        if (error) {
            *error = message.isEmpty() ? QStringLiteral("服务端返回错误") : sanitizeMessage(message);
        }
        return false;
    }

    const QJsonArray data = object.value(QStringLiteral("data")).toArray();
    if (data.size() != expectedCount) {
        if (error) {
            *error = QStringLiteral("返回向量数量不符（期望 %1，实际 %2）")
                         .arg(expectedCount)
                         .arg(data.size());
        }
        return false;
    }

    QList<QVector<float>> ordered;
    ordered.resize(expectedCount);  // QVector 即 QList：resize 生成默认空向量占位
    QList<bool> seen(expectedCount, false);
    for (int position = 0; position < data.size(); ++position) {
        const QJsonObject entry = data.at(position).toObject();
        const int index = entry.contains(QStringLiteral("index"))
                              ? entry.value(QStringLiteral("index")).toInt(-1)
                              : position;
        if (index < 0 || index >= expectedCount || seen.at(index)) {
            if (error) {
                *error = QStringLiteral("返回的 index 非法或重复");
            }
            return false;
        }

        const QJsonArray values = entry.value(QStringLiteral("embedding")).toArray();
        if (values.isEmpty()) {
            if (error) {
                *error = QStringLiteral("存在空向量");
            }
            return false;
        }
        if (m_dimensions > 0 && values.size() != m_dimensions) {
            if (error) {
                *error = QStringLiteral("向量维度不符（期望 %1，返回 %2）")
                             .arg(m_dimensions)
                             .arg(values.size());
            }
            return false;
        }
        if (m_dimensions == 0) {
            m_dimensions = values.size();
        }

        QVector<float> vector;
        vector.reserve(values.size());
        for (const QJsonValue &value : values) {
            vector.append(static_cast<float>(value.toDouble()));
        }
        ordered[index] = vector;
        seen[index] = true;
    }

    const ChatUsage usage = parseUsage(object.value(QStringLiteral("usage")).toObject());
    m_usage.promptTokens = addUsageField(m_usage.promptTokens, usage.promptTokens);
    m_usage.totalTokens = addUsageField(m_usage.totalTokens, usage.totalTokens);

    for (const QVector<float> &vector : ordered) {
        m_vectors.append(vector);
    }
    return true;
}

void EmbeddingService::fail(const ChatError &error)
{
    m_busy = false;
    m_handle = 0;
    m_inputs.clear();
    m_vectors.clear();
    m_body.clear();
    emit failed(error);
}

void EmbeddingService::finishSuccess()
{
    EmbeddingResult result;
    result.ok = true;
    result.vectors = m_vectors;
    result.usage = m_usage;
    result.dimensions = m_dimensions;

    m_busy = false;
    m_handle = 0;
    m_inputs.clear();
    m_vectors.clear();
    m_body.clear();
    emit finished(result);
}
