#include "core/chat/ChatController.h"

#include "providers/ProviderFactory.h"
#include "storage/SecureStore.h"

namespace {

QString normalizedProviderId(const QString &providerId)
{
    const QString id = providerId.trimmed().toLower();
    return id.isEmpty() ? QStringLiteral("openai") : id;
}

AiProvider::Protocol protocolFor(const QString &providerId)
{
    return providerId == QStringLiteral("anthropic") ? AiProvider::Anthropic : AiProvider::OpenAI;
}

QString defaultModelFor(const QString &providerId)
{
    return providerId == QStringLiteral("anthropic") ? QStringLiteral("claude-3-5-sonnet-latest")
                                                     : QStringLiteral("gpt-4o-mini");
}

}  // namespace

ChatController::ChatController(SecureStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    setProvider(m_providerId);
}

ChatController::~ChatController() = default;

QString ChatController::providerId() const
{
    return m_providerId;
}

QString ChatController::model() const
{
    return m_model;
}

void ChatController::setModel(const QString &model)
{
    if (m_model == model)
        return;
    m_model = model;
    emit modelChanged();
}

bool ChatController::busy() const
{
    return m_busy;
}

QString ChatController::replyText() const
{
    return m_replyText;
}

QString ChatController::lastError() const
{
    return m_lastError;
}

bool ChatController::hasApiKey() const
{
    return m_store && m_store->has(SecureStore::providerApiKeyId(m_providerId));
}

bool ChatController::dataStoreAvailable() const
{
    return m_store && m_store->isAvailable();
}

bool ChatController::imagesConfirmed() const
{
    return m_imagesConfirmed;
}

void ChatController::setImagesConfirmed(bool confirmed)
{
    if (m_imagesConfirmed == confirmed)
        return;
    m_imagesConfirmed = confirmed;
    emit imagesConfirmedChanged();
}

void ChatController::setProvider(const QString &providerId)
{
    m_providerId = normalizedProviderId(providerId);
    if (m_busy)
        cancel();

    m_provider = ProviderFactory::create(protocolFor(m_providerId), nullptr);
    if (m_provider) {
        connect(m_provider.get(), &AiProvider::chatDelta, this, [this](const QString &delta) {
            m_replyText += delta;
            emit replyTextChanged();
        });
        connect(m_provider.get(), &AiProvider::chatFinished, this, [this](const ChatResponse &response) {
            if (!response.text.isEmpty()) {
                m_replyText = response.text;
                emit replyTextChanged();
            }
            setBusy(false);
        });
        connect(m_provider.get(), &AiProvider::chatFailed, this, [this](const ChatError &error) {
            // 取消属于预期操作：保留已接收内容，不视为错误
            if (error.code != ChatError::Cancelled)
                setLastError(error.message);
            setBusy(false);
        });
    }

    m_model = defaultModelFor(m_providerId);
    emit providerChanged();
    emit modelChanged();
    emit apiKeyStateChanged();
}

bool ChatController::saveApiKey(const QString &apiKey)
{
    if (!m_store) {
        setLastError(QStringLiteral("安全存储不可用"));
        return false;
    }
    QString error;
    if (!m_store->save(SecureStore::providerApiKeyId(m_providerId), apiKey, &error)) {
        setLastError(error.isEmpty() ? QStringLiteral("保存密钥失败") : error);
        return false;
    }
    setLastError(QString());
    emit apiKeyStateChanged();
    return true;
}

bool ChatController::eraseApiKey()
{
    if (!m_store) {
        setLastError(QStringLiteral("安全存储不可用"));
        return false;
    }
    QString error;
    if (!m_store->erase(SecureStore::providerApiKeyId(m_providerId), &error)) {
        setLastError(error.isEmpty() ? QStringLiteral("删除密钥失败") : error);
        return false;
    }
    emit apiKeyStateChanged();
    return true;
}

void ChatController::send(const QString &text)
{
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;

    ChatRequest request;
    request.model = m_model;
    request.messages.append(message);
    sendRequest(request);
}

void ChatController::sendWithImage(const QString &text, const QString &imagePath)
{
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;
    ChatImage image;
    image.path = imagePath;
    message.images.append(image);

    ChatRequest request;
    request.model = m_model;
    request.messages.append(message);
    sendRequest(request);
}

void ChatController::sendRequest(const ChatRequest &request)
{
    if (m_busy) {
        setLastError(QStringLiteral("上一条请求尚未结束"));
        return;
    }
    if (!m_store) {
        setLastError(QStringLiteral("安全存储不可用，无法读取 API Key"));
        return;
    }

    QString apiKey;
    QString error;
    if (!m_store->load(SecureStore::providerApiKeyId(m_providerId), &apiKey, &error)
        || apiKey.isEmpty()) {
        setLastError(error.isEmpty() ? QStringLiteral("未配置 API Key")
                                     : QStringLiteral("读取 API Key 失败：%1").arg(error));
        return;
    }

    ensureProvider();
    if (!m_provider) {
        setLastError(QStringLiteral("Provider 不可用"));
        return;
    }

    ProviderConfig config;
    config.apiKey = apiKey;
    config.model = m_model;
    m_provider->configure(config);

    ChatRequest effective = request;
    effective.model = request.model.isEmpty() ? m_model : request.model;
    // 图片是否可上传由用户的显式确认开关决定（见 docs/PROMPT.md 硬性约束）
    effective.imagesConfirmed = m_imagesConfirmed;

    m_replyText.clear();
    emit replyTextChanged();
    setLastError(QString());
    setBusy(true);
    m_provider->sendChat(effective);
}

void ChatController::cancel()
{
    if (m_provider && m_busy)
        m_provider->cancel();
}

void ChatController::ensureProvider()
{
    if (m_provider)
        return;
    setProvider(m_providerId);
}

void ChatController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ChatController::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}
