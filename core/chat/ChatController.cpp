#include "core/chat/ChatController.h"

#include "core/ai/ProviderCatalog.h"
#include "core/chat/ChatHistoryModel.h"
#include "core/retriever/FileLevelRetriever.h"
#include "core/retriever/RagRetriever.h"
#include "core/retriever/ChunkSplitter.h"
#include "core/retriever/InjectionPlanner.h"
#include "core/skill/SkillCatalog.h"
#include "core/web/WebSearchService.h"
#include "core/web/WebSearchTool.h"
#include "providers/ProviderFactory.h"
#include "storage/LibraryConfigStore.h"
#include "storage/LibraryStore.h"
#include "storage/SecureStore.h"
#include "storage/WebConfigStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocumentFragment>

#include <utility>

// BudgetCalculator 是纯静态 struct，直接 BudgetCalculator::calculate(...) 调用
// estimateTokens 是 ChunkSplitter.h 里的全局函数（已 include）

namespace {

/// 有界上下文上限：条数与字符数双重限制，避免请求无限增长
constexpr int kMaxHistoryMessages = 20;
constexpr int kMaxHistoryChars = 8000;

/// 工具调用轮数上限：防止模型反复要求联网导致死循环
constexpr int kMaxToolRounds = 3;

QString normalizedId(const QString &id)
{
    return id.trimmed().toLower();
}

/// catalog 的 protocol 字段 -> Provider 协议枚举（custom 走 OpenAI 兼容协议）
AiProvider::Protocol protocolFor(const QString &protocol)
{
    return protocol == QStringLiteral("anthropic") ? AiProvider::Anthropic : AiProvider::OpenAI;
}

QString credentialStatusName(CredentialStatus status)
{
    switch (status) {
    case CredentialStatus::NotConfigured:
        return QStringLiteral("notConfigured");
    case CredentialStatus::Valid:
        return QStringLiteral("valid");
    case CredentialStatus::Invalid:
        return QStringLiteral("invalid");
    case CredentialStatus::Unverifiable:
        return QStringLiteral("unverifiable");
    case CredentialStatus::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

/// 每次请求现算的「当前时间」系统提示：API 模型自身不知道「今天几号」，
/// 由应用侧注入（**不进会话历史、不入库**）。时区取系统本地时区。
QString currentTimeSystemMessage()
{
    const QDateTime now = QDateTime::currentDateTime();
    const int offsetSec = now.offsetFromUtc();
    const QChar sign = offsetSec < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const int absSec = qAbs(offsetSec);
    const QString tz = QStringLiteral("UTC%1%2:%3")
                           .arg(sign)
                           .arg(absSec / 3600, 2, 10, QLatin1Char('0'))
                           .arg((absSec % 3600) / 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("当前时间：%1（%2）。当用户询问「现在」「今天」「最近」等时间相关问题时，请以此时间为准。")
        .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm dddd")), tz);
}

}  // namespace

ChatController::ChatController(SecureStore *store, ChatHistoryModel *history,
                               const ProviderCatalog *catalog, AiConfigStore *configStore,
                               WebConfigStore *webConfigStore, WebSearchService *webSearch,
                               NetworkTransport *providerTransport, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_history(history)
    , m_catalog(catalog)
    , m_configStore(configStore)
    , m_webConfigStore(webConfigStore)
    , m_web(webSearch)
    , m_providerTransport(providerTransport)
{
    AiConfigData data;
    if (m_configStore) {
        QString error;
        data = m_configStore->load(&error);
        // 配置损坏 / 缺失不是致命错误：回退默认并只在 lastError 里提示
        if (!error.isEmpty())
            setLastError(error);
    }
    applyLoadedConfig(data);

    // 联网检索（v0.2.3 新模块）：开关与端点记忆在 config.json 的 web 段，密钥只走 SecureStore
    if (m_webConfigStore) {
        QString webError;
        const WebConfigData web = m_webConfigStore->load(&webError);
        m_webEnabled = web.enabled;
        m_webEndpoint = web.endpoint;
        if (!webError.isEmpty() && m_lastError.isEmpty())
            setLastError(webError);
    }

    if (m_web) {
        // 联网检索（B 形态）：把检索能力包装成模型工具；具体生效方案在每次发消息前按当前厂商解析
        m_webTool = std::make_unique<WebSearchTool>(m_web);
        connect(m_webTool.get(), &WebSearchTool::finished, this,
                &ChatController::handleToolSearchFinished);
    }
}

ChatController::~ChatController() = default;

QString ChatController::providerId() const
{
    return m_providerId;
}

QString ChatController::modelId() const
{
    return m_modelId;
}

QString ChatController::thinkingLevel() const
{
    return m_thinkingLevel;
}

QStringList ChatController::thinkingLevels() const
{
    if (!m_catalog)
        return QStringList();
    return m_catalog->provider(m_providerId).model(m_modelId).thinking.levels;
}

QString ChatController::credentialStatus() const
{
    return credentialStatusName(m_credentialStatus);
}

QString ChatController::credentialMessage() const
{
    return m_credentialMessage;
}

bool ChatController::validating() const
{
    return m_validating;
}

bool ChatController::customizable() const
{
    return m_catalog && m_catalog->provider(m_providerId).customizable;
}

QString ChatController::customBaseUrl() const
{
    return m_config.customBaseUrl;
}

bool ChatController::modelListFetchable() const
{
    if (!m_catalog)
        return false;
    // 自定义厂商没有 catalog 的静态声明：允许直接尝试 /models，失败仅提示、不影响对话
    return m_catalog->provider(m_providerId).validationMethod == QStringLiteral("models")
        || customizable();
}

QString ChatController::modelListMessage() const
{
    return m_modelListMessage;
}

bool ChatController::supportsVision() const
{
    if (!m_catalog)
        return false;
    return m_catalog->provider(m_providerId).model(m_modelId).supportsVision;
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

bool ChatController::webSearchEnabled() const
{
    return m_webEnabled;
}

void ChatController::setWebSearchEnabled(bool enabled)
{
    if (m_webEnabled == enabled)
        return;
    m_webEnabled = enabled;
    persistWebConfig();
    emit webSearchChanged();
}

QString ChatController::webSearchEndpoint() const
{
    return m_webEndpoint;
}

void ChatController::setWebSearchEndpoint(const QString &endpoint)
{
    const QString value = endpoint.trimmed();
    if (m_webEndpoint == value)
        return;
    m_webEndpoint = value;
    persistWebConfig();
    emit webSearchChanged();
}

bool ChatController::webSearchNative() const
{
    return m_catalog && m_catalog->provider(m_providerId).nativeSearch.isValid();
}

bool ChatController::webSearchSearching() const
{
    return m_searching;
}

bool ChatController::webSearchReady() const
{
    bool supported = false;
    const WebSearchConfig config = effectiveSearchConfig(&supported, nullptr);
    return supported && !config.endpoint.trimmed().isEmpty() && !config.apiKey.trimmed().isEmpty();
}

QString ChatController::webSearchMessage() const
{
    bool supported = false;
    const WebSearchConfig config = effectiveSearchConfig(&supported, nullptr);
    if (!supported)
        return QStringLiteral("当前厂商暂不支持联网搜索；可在「联网搜索」里填写自定义搜索源");
    if (config.endpoint.trimmed().isEmpty())
        return QStringLiteral("联网搜索未配置：缺少搜索端点");
    if (config.apiKey.trimmed().isEmpty()) {
        return webSearchNative()
                   ? QStringLiteral("联网搜索未配置：请先为该厂商配置 API Key")
                   : QStringLiteral("联网搜索未配置：请先保存搜索密钥");
    }
    return QString();
}

bool ChatController::hasWebSearchKey() const
{
    return m_store && m_store->has(SecureStore::webSearchApiKeyId());
}

QVariantList ChatController::webSearchSources() const
{
    return m_webSources;
}

bool ChatController::saveWebSearchKey(const QString &apiKey)
{
    if (!m_store) {
        setLastError(QStringLiteral("安全存储不可用"));
        return false;
    }
    QString error;
    if (!m_store->save(SecureStore::webSearchApiKeyId(), apiKey, &error)) {
        setLastError(error.isEmpty() ? QStringLiteral("保存搜索密钥失败") : error);
        return false;
    }
    setLastError(QString());
    emit webSearchChanged();
    return true;
}

bool ChatController::eraseWebSearchKey()
{
    if (!m_store) {
        setLastError(QStringLiteral("安全存储不可用"));
        return false;
    }
    QString error;
    if (!m_store->erase(SecureStore::webSearchApiKeyId(), &error)) {
        setLastError(error.isEmpty() ? QStringLiteral("删除搜索密钥失败") : error);
        return false;
    }
    emit webSearchChanged();
    return true;
}

void ChatController::persistWebConfig()
{
    if (!m_webConfigStore)
        return;
    WebConfigData data;
    data.enabled = m_webEnabled;
    data.endpoint = m_webEndpoint;
    QString error;
    if (!m_webConfigStore->save(data, &error))
        setLastError(error);
}

QVariantList ChatController::providerOptions() const
{
    return m_catalog ? m_catalog->providerOptions() : QVariantList();
}

QVariantList ChatController::modelOptions(const QString &providerId) const
{
    QVariantList options = m_catalog ? m_catalog->modelOptions(providerId) : QVariantList();

    // 追加“手动拉取”得到的额外模型：无档位 / 无视觉声明，UI 按保守能力渲染
    const QStringList fetched = m_fetchedModels.value(normalizedId(providerId));
    for (const QString &id : fetched) {
        bool exists = false;
        for (const QVariant &entry : options) {
            if (entry.toMap().value(QStringLiteral("id")).toString() == id) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;

        QVariantMap map;
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("displayName"), id);
        map.insert(QStringLiteral("supportsVision"), false);
        map.insert(QStringLiteral("isDefault"), false);
        map.insert(QStringLiteral("levels"), QStringList());
        map.insert(QStringLiteral("supportsThinking"), false);
        map.insert(QStringLiteral("fetched"), true);
        options.append(map);
    }
    return options;
}

QVariantMap ChatController::modelInfo(const QString &providerId, const QString &modelId) const
{
    return m_catalog ? m_catalog->modelInfo(providerId, modelId) : QVariantMap();
}

QVariantMap ChatController::effectiveParams() const
{
    QJsonObject params;
    const QString key = selectionKey();
    if (m_config.nativeOverrides.contains(key))
        params = m_config.nativeOverrides.value(key);
    else if (m_catalog)
        params = m_catalog->provider(m_providerId).model(m_modelId).thinking.nativeFor(m_thinkingLevel);
    return params.toVariantMap();
}

QVariantMap ChatController::advancedParams() const
{
    return m_config.nativeOverrides.value(selectionKey()).toVariantMap();
}

QString ChatController::defaultProviderId() const
{
    if (m_catalog && !m_catalog->providerIds().isEmpty())
        return m_catalog->providerIds().first();
    return QStringLiteral("openai");
}

QString ChatController::defaultModelId(const QString &providerId) const
{
    if (!m_catalog)
        return QStringLiteral("gpt-4o-mini");
    // 自定义厂商没有内置模型：返回空串，由用户手填
    return m_catalog->provider(providerId).defaultModelId();
}

QString ChatController::defaultThinkingLevel(const QString &providerId, const QString &modelId) const
{
    if (!m_catalog)
        return QString();
    // 默认取厂商声明的**最高档**（catalog 中按由弱到强排列），语义可预测
    const QStringList levels = m_catalog->provider(providerId).model(modelId).thinking.levels;
    return levels.isEmpty() ? QString() : levels.last();
}

QString ChatController::selectionKey() const
{
    return AiConfigStore::selectionKey(m_providerId, m_modelId);
}

ProviderConfig ChatController::buildProviderConfig(const QString &apiKey) const
{
    ProviderConfig config;
    config.apiKey = apiKey;
    config.model = m_modelId;
    config.providerId = m_providerId;
    if (m_catalog) {
        const CatalogProvider entry = m_catalog->provider(m_providerId);
        // 自定义厂商使用用户手填的 API 链接，其余厂商一律用 catalog 中的官方 baseUrl
        config.baseUrl = entry.customizable ? m_config.customBaseUrl : entry.baseUrl;
        config.authStyle = entry.authStyle;
        config.validationMethod = entry.validationMethod;
        config.extraHeaders = entry.extraHeaders;
    }
    return config;
}

void ChatController::applyLoadedConfig(const AiConfigData &data)
{
    m_config = data;

    QString providerId = normalizedId(data.providerId);
    if (!m_catalog || !m_catalog->providerIds().contains(providerId))
        providerId = defaultProviderId();
    m_providerId = providerId;

    QString modelId = data.modelId.trimmed();
    if (modelId.isEmpty())
        modelId = defaultModelId(m_providerId);
    m_modelId = modelId;
    m_modelByProvider.insert(m_providerId, m_modelId);

    QString level = data.thinkingLevels.value(selectionKey());
    const QStringList levels = thinkingLevels();
    if (level.isEmpty() || !levels.contains(level))
        level = defaultThinkingLevel(m_providerId, m_modelId);
    m_thinkingLevel = level;

    rebuildProvider();
}

void ChatController::rebuildProvider()
{
    if (m_busy)
        cancel();

    if (!m_catalog) {
        m_provider = ProviderFactory::create(AiProvider::OpenAI, m_providerTransport);
    } else {
        const CatalogProvider entry = m_catalog->provider(m_providerId);
        m_provider = ProviderFactory::create(protocolFor(entry.protocol), m_providerTransport);
    }
    if (!m_provider)
        return;

    connect(m_provider.get(), &AiProvider::chatDelta, this, [this](const QString &delta) {
        m_replyText += delta;
        emit replyTextChanged();
        if (m_history && m_history->available())
            m_history->appendAssistantDelta(delta);
    });
    connect(m_provider.get(), &AiProvider::chatFinished, this, [this](const ChatResponse &response) {
        // 工具循环进行中：本轮可能是「要求调用工具」，交由循环处理
        if (m_toolLoop) {
            handleToolLoopFinished(response);
            return;
        }
        finishAssistantRound(response);
    });
    connect(m_provider.get(), &AiProvider::chatFailed, this, [this](const ChatError &error) {
        // 工具循环遇到失败一律终止，避免半途状态残留
        m_toolLoop = false;
        m_toolQueue.clear();
        m_toolQueueIndex = 0;
        m_searching = false;
        // 取消属于预期操作：保留已接收内容，不视为错误
        if (error.code != ChatError::Cancelled)
            setLastError(error.message);
        // 中断/失败都**保留已生成的部分内容**，只标记状态，不写坏数据
        const qint64 elapsed = m_roundTimer.isValid() ? m_roundTimer.elapsed() : -1;
        m_roundTimer.invalidate();
        if (m_history && m_history->available()) {
            m_history->finalizeAssistantMessage(error.code == ChatError::Cancelled
                                                    ? QStringLiteral("partial")
                                                    : QStringLiteral("error"),
                                                QString(), ChatUsage(), elapsed);
        }
        setBusy(false);
    });
    connect(m_provider.get(), &AiProvider::credentialsChecked, this,
            [this](CredentialStatus status, const QString &message) {
                setValidating(false);
                setCredential(status, message);
            });
    connect(m_provider.get(), &AiProvider::modelsFetched, this,
            [this](const QStringList &modelIds, const QString &error) {
                // 失败**不清空**已有清单，只把原因交给 UI 提示
                m_modelListMessage = error;
                if (error.isEmpty() && !modelIds.isEmpty()) {
                    m_fetchedModels.insert(m_providerId, modelIds);
                    const QHash<QString, int> windows = m_provider->modelContextWindows();
                    if (!windows.isEmpty()) {
                        m_modelContextWindows.insert(m_providerId, windows);
                    }
                }
                emit modelListChanged();
            });
}

void ChatController::persist()
{
    m_config.providerId = m_providerId;
    m_config.modelId = m_modelId;
    if (!m_configStore)
        return;

    QString error;
    if (!m_configStore->save(m_config, &error))
        setLastError(error);
}

void ChatController::setProvider(const QString &providerId)
{
    QString id = normalizedId(providerId);
    if (m_catalog && !m_catalog->providerIds().contains(id))
        return;
    if (id.isEmpty() || id == m_providerId)
        return;

    m_providerId = id;
    // 先恢复该厂商本次会话内记住的模型，否则用 catalog 默认模型
    QString modelId = m_modelByProvider.value(id);
    if (modelId.isEmpty())
        modelId = defaultModelId(id);
    m_modelId = modelId;
    m_modelByProvider.insert(id, modelId);

    QString level = m_config.thinkingLevels.value(selectionKey());
    const QStringList levels = thinkingLevels();
    if (level.isEmpty() || !levels.contains(level))
        level = defaultThinkingLevel(id, m_modelId);
    m_thinkingLevel = level;

    m_modelListMessage.clear();
    setValidating(false);
    setCredential(CredentialStatus::Unknown, QString());
    rebuildProvider();
    persist();

    emit providerChanged();
    emit modelChanged();
    emit thinkingChanged();
    emit apiKeyStateChanged();
    emit modelListChanged();
    // 联网能力随厂商变化（内置搜索 / 复用 Key 的可用性不同）
    emit webSearchChanged();
}

void ChatController::setModelId(const QString &modelId)
{
    const QString id = modelId.trimmed();
    if (id == m_modelId)
        return;

    m_modelId = id;
    m_modelByProvider.insert(m_providerId, id);

    QString level = m_config.thinkingLevels.value(selectionKey());
    const QStringList levels = thinkingLevels();
    if (level.isEmpty() || !levels.contains(level))
        level = defaultThinkingLevel(m_providerId, m_modelId);
    m_thinkingLevel = level;

    setValidating(false);
    setCredential(CredentialStatus::Unknown, QString());
    rebuildProvider();
    persist();

    emit modelChanged();
    emit thinkingChanged();
}

void ChatController::setThinkingLevel(const QString &level)
{
    const QString value = level.trimmed();
    // 未声明的档位不出现、不生效（UI 只渲染 catalog 声明的档位）
    if (!value.isEmpty() && !thinkingLevels().contains(value))
        return;
    if (value == m_thinkingLevel)
        return;

    m_thinkingLevel = value;
    m_config.thinkingLevels.insert(selectionKey(), value);
    // 手动选档位即取消高级覆盖，保证“档位 / 覆盖”语义唯一
    m_config.nativeOverrides.remove(selectionKey());
    persist();
    emit thinkingChanged();
}

QString ChatController::systemPrompt() const
{
    return m_systemPrompt;
}

void ChatController::setSystemPrompt(const QString &prompt)
{
    if (m_systemPrompt == prompt)
        return;
    // 只改内存态：下一条消息生效，不重写历史消息、不触发任何持久化
    m_systemPrompt = prompt;
    emit systemPromptChanged();
}

void ChatController::setCustomBaseUrl(const QString &baseUrl)
{
    const QString value = baseUrl.trimmed();
    if (m_config.customBaseUrl == value)
        return;
    m_config.customBaseUrl = value;
    setCredential(CredentialStatus::Unknown, QString());
    persist();
    emit providerChanged();
}

void ChatController::setAdvancedParams(const QVariantMap &params)
{
    if (params.isEmpty()) {
        clearAdvancedParams();
        return;
    }
    m_config.nativeOverrides.insert(selectionKey(), QJsonObject::fromVariantMap(params));
    persist();
    emit thinkingChanged();
}

void ChatController::clearAdvancedParams()
{
    if (!m_config.nativeOverrides.contains(selectionKey()))
        return;
    m_config.nativeOverrides.remove(selectionKey());
    persist();
    emit thinkingChanged();
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
    setCredential(CredentialStatus::Unknown, QString());
    emit apiKeyStateChanged();
    // 内置搜索复用厂商 Key：Key 变化直接影响联网就绪状态
    emit webSearchChanged();
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
    setCredential(CredentialStatus::NotConfigured, QStringLiteral("未配置 API Key"));
    emit apiKeyStateChanged();
    emit webSearchChanged();
    return true;
}

void ChatController::validateApiKey()
{
    ensureProvider();
    if (!m_provider) {
        setCredential(CredentialStatus::Unknown, QStringLiteral("Provider 不可用"));
        return;
    }
    if (!m_store || !m_store->isAvailable()) {
        setCredential(CredentialStatus::Unverifiable, QStringLiteral("本平台不支持安全存储，无法校验"));
        return;
    }

    QString apiKey;
    QString error;
    if (!m_store->load(SecureStore::providerApiKeyId(m_providerId), &apiKey, &error)
        || apiKey.isEmpty()) {
        setCredential(CredentialStatus::NotConfigured, QStringLiteral("未配置 API Key"));
        return;
    }
    if (customizable() && (m_config.customBaseUrl.isEmpty() || m_modelId.isEmpty())) {
        setCredential(CredentialStatus::Unverifiable,
                      QStringLiteral("请先填写 API 链接与模型名称"));
        return;
    }

    m_provider->configure(buildProviderConfig(apiKey));
    setValidating(true);
    m_provider->validateCredentials();
}

void ChatController::fetchModelList()
{
    ensureProvider();
    if (!m_provider) {
        m_modelListMessage = QStringLiteral("Provider 不可用");
        emit modelListChanged();
        return;
    }

    QString apiKey;
    QString loadError;
    if (!m_store || !m_store->load(SecureStore::providerApiKeyId(m_providerId), &apiKey, &loadError)
        || apiKey.isEmpty()) {
        m_modelListMessage = QStringLiteral("未配置 API Key");
        emit modelListChanged();
        return;
    }

    if (customizable() && m_config.customBaseUrl.trimmed().isEmpty()) {
        m_modelListMessage = QStringLiteral("自定义厂商需要先填写 API 链接");
        emit modelListChanged();
        return;
    }

    m_provider->configure(buildProviderConfig(apiKey));
    m_modelListMessage = QStringLiteral("正在拉取…");
    emit modelListChanged();
    // 自定义厂商的 catalog 校验方式是 chat-minimal，必须走「无条件尝试 /models」的入口
    if (customizable())
        m_provider->fetchModelsForService();
    else
        m_provider->fetchModels();
}

void ChatController::send(const QString &text)
{
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;
    dispatchUserMessage(message);
}

void ChatController::sendWithImage(const QString &text, const QString &imagePath)
{
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;
    ChatImage image;
    image.path = imagePath;
    message.images.append(image);
    dispatchUserMessage(message);
}

WebSearchConfig ChatController::effectiveSearchConfig(bool *supported, QString *notice) const
{
    if (supported)
        *supported = false;
    if (notice)
        notice->clear();

    WebSearchConfig config;

    if (!m_catalog || !m_store || !m_web) {
        if (notice)
            *notice = QStringLiteral("联网搜索模块未装配");
        return config;
    }

    const CatalogProvider entry = m_catalog->provider(m_providerId);

    // ① 厂商内置搜索（零配置）：端点来自 catalog，密钥默认复用该厂商 API Key
    if (entry.nativeSearch.isValid()) {
        if (supported)
            *supported = true;
        config.endpoint = entry.nativeSearch.searchEndpoint;
        config.fetchEndpoint = entry.nativeSearch.fetchEndpoint;
        config.protocol = entry.nativeSearch.protocol == QStringLiteral("deepseek")
                              ? WebSearchConfig::Protocol::DeepSeek
                              : WebSearchConfig::Protocol::Generic;
        if (m_store->isAvailable()) {
            QString apiKey;
            QString keyError;
            const QString keyId = entry.nativeSearch.reuseProviderKey
                                      ? SecureStore::providerApiKeyId(m_providerId)
                                      : SecureStore::webSearchApiKeyId();
            if (m_store->load(keyId, &apiKey, &keyError))
                config.apiKey = apiKey.trimmed();
        }
        return config;
    }

    // ② 厂商无内置搜索：使用用户自填的「自定义搜索源」（第三级菜单里配置）
    const QString custom = m_webEndpoint.trimmed();
    if (!custom.isEmpty()) {
        if (supported)
            *supported = true;
        config.endpoint = custom;
        config.protocol = WebSearchConfig::Protocol::Generic;
        if (m_store->isAvailable()) {
            QString apiKey;
            QString keyError;
            if (m_store->load(SecureStore::webSearchApiKeyId(), &apiKey, &keyError))
                config.apiKey = apiKey.trimmed();
        }
        return config;
    }

    // ③ 厂商不支持，且未填自定义源
    if (notice)
        *notice = QStringLiteral("当前厂商暂不支持联网搜索（可在「联网搜索」里填写自定义搜索源）");
    return config;
}

void ChatController::dispatchUserMessage(const ChatMessage &userMessage, bool recordUserMessage)
{
    if (m_busy) {
        setLastError(QStringLiteral("上一条请求尚未结束"));
        return;
    }
    if (!m_store) {
        setLastError(QStringLiteral("安全存储不可用，无法读取 API Key"));
        return;
    }
    // 自定义厂商必须填全，否则**不发请求**：避免把请求打到空地址上
    if (customizable() && (m_config.customBaseUrl.isEmpty() || m_modelId.isEmpty())) {
        setLastError(QStringLiteral("自定义厂商需要先填写 API 链接与模型名称"));
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

    // 资料库（A 形态）：挂载小库时先做应用侧预检索，拿到片段后再装配请求
    if (m_retriever && !m_mountedCollectionIds.isEmpty()) {
        beginLibraryRetrieval(userMessage, recordUserMessage);
        return;
    }

    continueDispatch(userMessage, apiKey, QString(), recordUserMessage);
}

void ChatController::continueDispatch(const ChatMessage &userMessage, const QString &apiKey,
                                      const QString &libraryContext, bool recordUserMessage)
{
    ensureProvider();
    if (!m_provider) {
        setLastError(QStringLiteral("Provider 不可用"));
        setBusy(false);
        return;
    }

    m_provider->configure(buildProviderConfig(apiKey));

    ChatRequest request;
    request.model = m_modelId;
    request.thinkingLevel = m_thinkingLevel;
    request.nativeParams = QJsonObject::fromVariantMap(effectiveParams());

    if (m_history && m_history->available()) {
        // 写入会话历史，再按**有界上下文**取回（含刚写入的这条用户消息）。
        // 重新生成时用户消息已在库中，若再次写入会重复，因此按 recordUserMessage 跳过。
        if (recordUserMessage)
            m_history->appendUserMessage(userMessage.text);
        m_pendingAssistantMessageId = m_history->beginAssistantMessage();
        request.messages = m_history->requestHistory(kMaxHistoryMessages, kMaxHistoryChars);
        // 图片不落库，仅在本次请求末尾补上（是否真正上传仍由 imagesConfirmed 决定）
        if (!userMessage.images.isEmpty() && !request.messages.isEmpty()
            && request.messages.last().role == ChatMessage::User) {
            request.messages.last().images = userMessage.images;
        }
    } else {
        m_pendingAssistantMessageId = 0;
        request.messages.append(userMessage);
    }

    // 注入当前时间：置顶且**不进会话历史、不入库**（API 模型自身不知道当前时间）
    ChatMessage timeContext;
    timeContext.role = ChatMessage::System;
    timeContext.text = currentTimeSystemMessage();
    request.messages.prepend(timeContext);

    // 系统提示注入顺序（固定契约）：角色卡 → 当前时间 → 技能 → 资料库。
    // 角色卡（M18）是人格设定，固定为**第一条** system 消息（index 0）；当前时间、技能、
    // 资料库都只是背景资料，必须排在其后，避免把角色设定挤到「资料」的位置。
    // 空提示词（含纯空白）一律不下发，避免出现空 system 消息。
    int slot = 1;
    if (!m_systemPrompt.trimmed().isEmpty()) {
        ChatMessage roleContext;
        roleContext.role = ChatMessage::System;
        roleContext.text = m_systemPrompt;
        request.messages.insert(0, roleContext);
        ++slot;  // 时间上下文被顶到 index 1，后续注入从 index 2 起
    }

    // 技能（功能 #9）：无命中时不注入空内容；预算与截断由 SkillCatalog 统一处理
    const QString skillContext = buildSkillContext(userMessage.text);
    if (!skillContext.isEmpty()) {
        ChatMessage skillMessage;
        skillMessage.role = ChatMessage::System;
        skillMessage.text = skillContext;
        request.messages.insert(slot, skillMessage);
        ++slot;
    }

    // 资料库片段紧随技能之后：只作为「资料」，不得被当成指令执行
    if (!libraryContext.isEmpty()) {
        ChatMessage libraryMessage;
        libraryMessage.role = ChatMessage::System;
        libraryMessage.text = libraryContext;
        request.messages.insert(slot, libraryMessage);
    }

    // 联网检索（B 形态）：开关打开时把「联网搜索」作为工具交给模型，由模型**自行决定**是否调用。
    // 厂商不支持 / 未就绪：明确提示后**降级为普通对话**（不崩、不清空）。
    QString notice;
    if (m_webEnabled && m_webTool) {
        bool supported = false;
        QString planNotice;
        const WebSearchConfig config = effectiveSearchConfig(&supported, &planNotice);
        if (supported && !config.endpoint.trimmed().isEmpty() && !config.apiKey.trimmed().isEmpty()) {
            m_webTool->configure(config);
            request.tools = QJsonArray{WebSearchTool::descriptor()};
            request.toolChoice = QStringLiteral("auto");

            m_webSources.clear();
            emit webSearchSourcesChanged();

            m_loopRequest = request;
            m_toolQueue.clear();
            m_toolQueueIndex = 0;
            m_toolRounds = 0;
            m_toolLoop = true;
        } else {
            notice = webSearchMessage();
        }
    }
    if (!notice.isEmpty())
        notice += QStringLiteral("（已按普通对话回答）");

    sendRequest(request);
    // sendRequest 会清空 lastError：降级提示在此重新亮出，避免用户误以为联网已生效
    if (!notice.isEmpty())
        setLastError(notice);
}

void ChatController::attachLibrary(LibraryStore *store, RagRetriever *retriever,
                                   LibraryConfigStore *configStore,
                                   FileLevelRetriever *fileRetriever)
{
    m_libraryStore = store;
    m_retriever = retriever;
    m_fileRetriever = fileRetriever;
    m_libraryConfigStore = configStore;

    if (m_retriever) {
        connect(m_retriever, &RagRetriever::finished, this,
                [this](const RetrievalResult &result) { handleLibraryRetrievalFinished(result); });
        connect(m_retriever, &RagRetriever::failed, this,
                [this](const ChatError &error) { handleLibraryRetrievalFailed(error); });
    }

    if (m_libraryConfigStore) {
        QString error;
        const LibraryConfigData data = m_libraryConfigStore->load(&error);
        m_mountedCollectionIds = data.mountedCollections;
        if (m_retriever) {
            RetrievalConfig config;
            config.topK = data.topK;
            config.perFileLimit = data.perFileLimit;
            config.tokenBudget = data.tokenBudget;
            config.similarityThreshold = data.similarityThreshold;
            config.neighborRadius = data.neighborRadius;
            config.ftsFallback = data.ftsFallback;
            m_retriever->setConfig(config);
            m_retriever->setEmbeddingModel(data.embedding.providerId, data.embedding.modelId);
        }
        emit libraryChanged();
    }
}

void ChatController::attachSkills(SkillCatalog *skills)
{
    if (m_skills == skills) {
        return;
    }
    if (m_skills) {
        disconnect(m_skills, nullptr, this, nullptr);
    }
    m_skills = skills;

    if (m_skills) {
        // 模式切换直接影响输入区入口的显隐
        connect(m_skills, &SkillCatalog::modeChanged, this, &ChatController::skillSelectionChanged);
        // 技能被禁用 / 删除后剔除已失效的手动选择，避免下一轮注入幽灵技能
        connect(m_skills, &SkillCatalog::skillsChanged, this, [this] {
            QStringList kept;
            const QStringList available = m_skills ? m_skills->ids() : QStringList();
            for (const QString &id : std::as_const(m_pendingSkills)) {
                if (available.contains(id) && m_skills->isEnabled(id)) {
                    kept << id;
                }
            }
            if (kept != m_pendingSkills) {
                m_pendingSkills = kept;
                emit skillSelectionChanged();
            }
        });
    }

    m_pendingSkills.clear();
    emit skillSelectionChanged();
}

QStringList ChatController::pendingSkills() const
{
    return m_pendingSkills;
}

bool ChatController::skillManualMode() const
{
    return m_skills && m_skills->manualMode();
}

void ChatController::togglePendingSkill(const QString &id)
{
    // 只有手动模式才允许选择；自动模式的命中完全由触发词决定
    if (!m_skills || !m_skills->manualMode()) {
        return;
    }
    if (!m_skills->ids().contains(id) || !m_skills->isEnabled(id)) {
        return; // 禁用 / 不存在的技能永不被选
    }

    const int index = m_pendingSkills.indexOf(id);
    if (index >= 0) {
        m_pendingSkills.removeAt(index);
        emit skillSelectionChanged();
        return;
    }
    if (m_pendingSkills.size() >= SkillCatalog::kMaxAutoSkills) {
        return; // 上限 2 个，超出忽略
    }
    m_pendingSkills << id;
    emit skillSelectionChanged();
}

void ChatController::clearPendingSkills()
{
    if (m_pendingSkills.isEmpty()) {
        return;
    }
    m_pendingSkills.clear();
    emit skillSelectionChanged();
}

QString ChatController::buildSkillContext(const QString &userText)
{
    if (!m_skills) {
        return {};
    }

    const SkillCatalog::Injection injection = m_skills->composeInjection(userText, m_pendingSkills);

    // 手动选择只对本次发送生效：无论是否命中都清空，避免下一轮误注入
    if (!m_pendingSkills.isEmpty()) {
        m_pendingSkills.clear();
        emit skillSelectionChanged();
    }
    return injection.text;
}

QVariantList ChatController::mountedCollections() const
{
    QVariantList result;
    for (qint64 id : m_mountedCollectionIds) {
        result.append(id);
    }
    return result;
}

bool ChatController::libraryMounted() const
{
    return !m_mountedCollectionIds.isEmpty();
}

int ChatController::libraryHitCount() const
{
    return m_lastRetrieval.chunks.size();
}

int ChatController::libraryTokenCount() const
{
    return m_lastRetrieval.tokenEst;
}

bool ChatController::libraryDegraded() const
{
    return m_lastRetrieval.degraded;
}

QString ChatController::libraryStatus() const
{
    return m_libraryStatus;
}

QVariantList ChatController::librarySources() const
{
    QVariantList result;
    int index = 0;
    for (const RetrievedChunk &chunk : m_lastRetrieval.chunks) {
        ++index;
        QVariantMap source;
        source.insert(QStringLiteral("index"), index);
        source.insert(QStringLiteral("fileName"), chunk.fileName);
        source.insert(QStringLiteral("locLabel"), chunk.locLabel);
        source.insert(QStringLiteral("locKind"), chunk.locKind);
        source.insert(QStringLiteral("locPage"), chunk.locPage);
        source.insert(QStringLiteral("locSheet"), chunk.locSheet);
        source.insert(QStringLiteral("locRowFrom"), chunk.locRowFrom);
        source.insert(QStringLiteral("locRowTo"), chunk.locRowTo);
        source.insert(QStringLiteral("chunkId"), chunk.chunkId);
        source.insert(QStringLiteral("fileId"), chunk.fileId);
        source.insert(QStringLiteral("collectionId"), chunk.collectionId);
        source.insert(QStringLiteral("score"), chunk.score);
        QString snippet = chunk.text.simplified();
        if (snippet.size() > 160) {
            snippet = snippet.left(160) + QStringLiteral("…");
        }
        source.insert(QStringLiteral("snippet"), snippet);
        result.append(source);
    }
    return result;
}

QVariantList ChatController::libraryInjections() const
{
    QVariantList result;
    for (const DocumentInjectionResult &injection : m_lastInjections) {
        QString level;
        switch (injection.level) {
        case InjectionLevel::FullText:
            level = QStringLiteral("全文");
            break;
        case InjectionLevel::Section:
            level = QStringLiteral("章节");
            break;
        case InjectionLevel::Fragment:
            level = QStringLiteral("片段");
            break;
        case InjectionLevel::None:
            continue;
        }

        QVariantMap item;
        item.insert(QStringLiteral("fileId"), injection.fileId);
        item.insert(QStringLiteral("fileName"), injection.fileName);
        item.insert(QStringLiteral("level"), level);
        item.insert(QStringLiteral("tokenEst"), injection.tokenEst);
        item.insert(QStringLiteral("truncated"), injection.truncated);
        item.insert(QStringLiteral("approximate"), injection.sectionSource == QStringLiteral("page_approx"));
        item.insert(QStringLiteral("trace"), injection.trace);
        result.append(item);
    }
    return result;
}

void ChatController::setMountedCollections(const QVariantList &collectionIds)
{
    QList<qint64> ids;
    for (const QVariant &value : collectionIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id > 0 && !ids.contains(id)) {
            ids.append(id);
        }
    }
    if (ids == m_mountedCollectionIds) {
        return;
    }
    m_mountedCollectionIds = ids;
    m_libraryStatus.clear();
    emit libraryChanged();
    persistMountedCollections();
}

void ChatController::clearMountedCollections()
{
    if (m_mountedCollectionIds.isEmpty()) {
        return;
    }
    m_mountedCollectionIds.clear();
    m_libraryStatus.clear();
    emit libraryChanged();
    persistMountedCollections();
}

void ChatController::beginLibraryRetrieval(const ChatMessage &userMessage, bool recordUserMessage)
{
    m_pendingUserMessage = userMessage;
    m_pendingRecordUserMessage = recordUserMessage;
    m_libraryRetrieving = true;
    m_replyText.clear();
    emit replyTextChanged();
    setLastError(QString());
    setBusy(true);

    m_lastRetrieval = RetrievalResult();
    m_lastInjections.clear();
    m_libraryStatus = QStringLiteral("正在检索资料库…");
    emit libraryChanged();
    emit librarySourcesChanged();

    // 检索统一走 RagRetriever 的异步路径（向量优先，FTS5 兜底）；
    // 文件级聚合与三级注入在结果回调里完成（见 handleLibraryRetrievalFinished）。
    m_retriever->setMountedCollections(m_mountedCollectionIds);
    syncRetrievalConfig();
    m_retriever->retrieveAsync(userMessage.text);
}

void ChatController::syncRetrievalConfig()
{
    if (!m_retriever || !m_libraryConfigStore)
        return;
    QString error;
    const LibraryConfigData data = m_libraryConfigStore->load(&error);
    if (!error.isEmpty())
        return;
    RetrievalConfig config;
    config.topK = data.topK;
    config.perFileLimit = data.perFileLimit;
    config.tokenBudget = data.tokenBudget;
    config.similarityThreshold = data.similarityThreshold;
    config.neighborRadius = data.neighborRadius;
    config.ftsFallback = data.ftsFallback;
    m_retriever->setConfig(config);
    m_retriever->setEmbeddingModel(data.embedding.providerId, data.embedding.modelId);
}

void ChatController::handleLibraryRetrievalFinished(const RetrievalResult &result)
{
    if (!m_libraryRetrieving) {
        return;
    }
    m_libraryRetrieving = false;
    m_lastRetrieval = result;

    // 文件级聚合 + 三级注入编排（v0.2.6）。未装配 FileLevelRetriever 时回退片段语义。
    QList<DocumentInjectionResult> injected;
    QString degradeNote;
    if (m_fileRetriever && m_libraryConfigStore) {
        QString configError;
        const LibraryConfigData config = m_libraryConfigStore->load(&configError);

        int historyTokens = 0;
        if (m_history && m_history->available()) {
            const auto messages = m_history->requestHistory(kMaxHistoryMessages, kMaxHistoryChars);
            for (const ChatMessage &message : messages) {
                historyTokens += estimateTokens(message.text);
            }
        }
        // 角色卡提示词（M18）同样占用注入预算：资料库注入据此收紧，两者合计不超既有预算
        historyTokens += estimateTokens(m_systemPrompt);

        injected = m_fileRetriever->planInjection(result.chunks, config,
                                                  contextWindowForCurrentModel(), historyTokens,
                                                  &degradeNote);
    }

    if (result.degraded) {
        m_libraryStatus = result.note.isEmpty()
                              ? QStringLiteral("已降级为关键词检索")
                              : result.note;
    } else if (injected.isEmpty()) {
        m_libraryStatus = QStringLiteral("资料库未命中相关文件");
    } else {
        int tokenTotal = 0;
        for (const DocumentInjectionResult &doc : injected) {
            tokenTotal += doc.tokenEst;
        }
        m_libraryStatus = QStringLiteral("已召回 %1 个相关文件 · 注入约 %2 token")
                              .arg(injected.size())
                              .arg(tokenTotal);
        if (!degradeNote.isEmpty()) {
            m_libraryStatus += QStringLiteral("（%1）").arg(degradeNote);
        }
    }
    m_lastRetrieval.tokenEst = 0;
    for (const DocumentInjectionResult &doc : injected) {
        m_lastRetrieval.tokenEst += doc.tokenEst;
    }
    m_lastInjections = injected;
    emit libraryChanged();
    emit librarySourcesChanged();

    // 密钥只在这一刻重新读取，避免长时间持有
    QString apiKey;
    QString error;
    if (!m_store
        || !m_store->load(SecureStore::providerApiKeyId(m_providerId), &apiKey, &error)
        || apiKey.isEmpty()) {
        setBusy(false);
        setLastError(error.isEmpty() ? QStringLiteral("读取 API Key 失败")
                                     : QStringLiteral("读取 API Key 失败：%1").arg(error));
        return;
    }

    continueDispatch(m_pendingUserMessage, apiKey,
                     m_fileRetriever ? buildLibraryContext(injected) : buildLibraryContext(result),
                     m_pendingRecordUserMessage);
}

void ChatController::handleLibraryRetrievalFailed(const ChatError &error)
{
    if (!m_libraryRetrieving) {
        return;
    }
    m_libraryRetrieving = false;
    m_lastRetrieval = RetrievalResult();
    m_lastRetrieval.degraded = true;
    m_lastRetrieval.note = error.message;
    m_lastInjections.clear();
    m_libraryStatus = error.message.isEmpty()
                          ? QStringLiteral("资料库检索失败，已按普通对话回答")
                          : QStringLiteral("资料库检索失败（%1），已按普通对话回答").arg(error.message);
    emit libraryChanged();
    emit librarySourcesChanged();

    QString apiKey;
    QString loadError;
    if (!m_store
        || !m_store->load(SecureStore::providerApiKeyId(m_providerId), &apiKey, &loadError)
        || apiKey.isEmpty()) {
        setBusy(false);
        setLastError(loadError.isEmpty() ? QStringLiteral("读取 API Key 失败")
                                         : QStringLiteral("读取 API Key 失败：%1").arg(loadError));
        return;
    }
    continueDispatch(m_pendingUserMessage, apiKey, QString(), m_pendingRecordUserMessage);
}

int ChatController::contextWindowForCurrentModel() const
{
    // 优先用厂商接口实际返回的窗口，其次 providers.json 内置值；都没有则为 0（未知）
    const QHash<QString, int> windows = m_modelContextWindows.value(m_providerId);
    const int discovered = windows.value(m_modelId, 0);
    if (discovered > 0) {
        return discovered;
    }
    if (!m_catalog || !m_catalog->available()) {
        return 0;
    }
    return m_catalog->provider(m_providerId).model(m_modelId).contextWindow;
}

// ---- v0.2.6: 仅供参考语义提示词组装 ----
QString ChatController::buildLibraryContext(const QList<DocumentInjectionResult> &docs)
{
    if (docs.isEmpty()) {
        // 无相关文件 → 不注入任何 system 消息
        return QString();
    }

    QString text = QStringLiteral(
        "以下文档**可能与用户的问题相关，仅作为参考**。\n"
        "你**不必**仅根据这些文档回答，可以结合你自身的知识，也可以使用联网搜索的结果。\n"
        "若使用了文档内容，请标注来源（文件名 + 页码/工作表/行号）。\n"
        "若文档未提及，请明确说明「资料库中未提及」，再用其他方式回答。\n");

    int docIndex = 0;
    for (const DocumentInjectionResult &d : docs) {
        ++docIndex;

        QString levelStr;
        switch (d.level) {
        case InjectionLevel::FullText:    levelStr = QStringLiteral("全文"); break;
        case InjectionLevel::Section:     levelStr = QStringLiteral("章节"); break;
        case InjectionLevel::Fragment:     levelStr = QStringLiteral("片段"); break;
        default:                           levelStr = QStringLiteral("未知"); break;
        }

        QString sourceNote;
        if (d.sectionSource == QStringLiteral("page_approx")) {
            sourceNote = QStringLiteral("（章节为近似切分）");
        }

        QString truncationNote;
        if (d.truncated) {
            truncationNote = QStringLiteral("（因预算限制，本文件内容已截断）");
        }

        text += QStringLiteral("\n── 文件 %1：%2（%3 注入，共 %4 tokens%5%6）──\n%7\n")
                    .arg(docIndex)
                    .arg(d.fileName)
                    .arg(levelStr)
                    .arg(d.tokenEst)
                    .arg(sourceNote)
                    .arg(truncationNote)
                    .arg(d.content);
    }
    return text;
}

// ---- 旧版本保留（FileLevelRetriever 未装配时回退）----
QString ChatController::buildLibraryContext(const RetrievalResult &result)
{
    if (result.chunks.isEmpty()) {
        return QString();  // v0.2.6 改：空结果不注入
    }

    QString text = QStringLiteral(
        "以下文档**可能与用户的问题相关，仅作为参考**。\n"
        "你**不必**仅根据这些文档回答，可以结合你自身的知识，也可以使用联网搜索的结果。\n"
        "若使用了文档内容，请标注来源（文件名 + 定位）。\n"
        "若文档未提及，请明确说明「资料库中未提及」，再用其他方式回答。\n");

    int index = 0;
    for (const RetrievedChunk &chunk : result.chunks) {
        ++index;
        text += QStringLiteral("\n[来源 %1] %2（%3）\n%4\n")
                    .arg(index)
                    .arg(chunk.fileName,
                         chunk.locLabel.isEmpty() ? QStringLiteral("片段 %1").arg(index)
                                                  : chunk.locLabel,
                         chunk.text);
    }
    return text;
}

void ChatController::persistMountedCollections()
{
    if (!m_libraryConfigStore) {
        return;
    }
    QString error;
    LibraryConfigData data = m_libraryConfigStore->load(&error);
    data.mountedCollections = m_mountedCollectionIds;
    m_libraryConfigStore->save(data, &error);
}

void ChatController::persistCitations(qint64 messageId)
{
    if (messageId <= 0 || !m_libraryStore || m_lastRetrieval.chunks.isEmpty()) {
        return;
    }
    int rank = 0;
    for (const RetrievedChunk &chunk : m_lastRetrieval.chunks) {
        ++rank;
        CitationRecord citation;
        citation.messageId = messageId;
        citation.conversationId = m_history ? m_history->conversationId() : 0;
        citation.chunkId = chunk.chunkId;
        citation.fileId = chunk.fileId;
        citation.collectionId = chunk.collectionId;
        citation.locLabel = chunk.locLabel;
        citation.rank = rank;
        citation.score = chunk.score;
        qint64 newId = 0;
        QString error;
        // 来源落库失败不应影响对话本身，静默跳过（不记录正文 / 不打印 Key）
        m_libraryStore->appendCitation(citation, &newId, &error);
    }
}


void ChatController::handleToolLoopFinished(const ChatResponse &response)
{
    const bool wantsTools =
        response.finishReason == ChatResponse::ToolCalls && !response.toolCalls.isEmpty();

    if (wantsTools && m_toolRounds < kMaxToolRounds) {
        ++m_toolRounds;
        // 把「助手发起工具调用」这一轮补进可携带消息（仅内存，不入库）
        ChatMessage assistantMessage;
        assistantMessage.role = ChatMessage::Assistant;
        assistantMessage.text = response.text;
        assistantMessage.toolCalls = response.toolCalls;
        m_loopRequest.messages.append(assistantMessage);

        m_toolQueue = response.toolCalls;
        m_toolQueueIndex = 0;
        m_replyText.clear();
        emit replyTextChanged();
        runNextToolCall();
        return;
    }

    if (wantsTools) {
        // 达到轮数上限：不再执行工具，明确提示后收尾
        m_toolLoop = false;
        setLastError(QStringLiteral("联网搜索已达最大轮数（%1 轮），停止继续检索").arg(kMaxToolRounds));
    } else {
        m_toolLoop = false;
    }
    finishAssistantRound(response);
}

void ChatController::runNextToolCall()
{
    if (!m_toolLoop)
        return;

    if (m_toolQueueIndex >= m_toolQueue.size()) {
        // 本轮工具全部执行完：带着 tool 结果再发一轮，让模型给出最终回答
        sendRequest(m_loopRequest);
        return;
    }

    const ChatToolCall call = m_toolQueue.at(m_toolQueueIndex);
    ++m_toolQueueIndex;
    m_currentToolCallId = call.id;

    // 未知工具 / 工具未装配：回填明确错误，让模型自行收敛（不崩）
    if (!m_webTool || call.name != WebSearchTool::toolName()) {
        ChatMessage toolMessage;
        toolMessage.role = ChatMessage::Tool;
        toolMessage.toolCallId = call.id;
        toolMessage.text = QStringLiteral("{\"error\":\"不支持的工具：%1\"}").arg(call.name);
        m_loopRequest.messages.append(toolMessage);
        runNextToolCall();
        return;
    }

    const QString query = toolQueryFrom(call.arguments);
    if (query.isEmpty()) {
        ChatMessage toolMessage;
        toolMessage.role = ChatMessage::Tool;
        toolMessage.toolCallId = call.id;
        toolMessage.text = QStringLiteral("{\"error\":\"缺少 query 参数\"}");
        m_loopRequest.messages.append(toolMessage);
        runNextToolCall();
        return;
    }

    m_searching = true;
    emit webSearchChanged();
    setBusy(true);
    m_webTool->execute(query);
}

void ChatController::handleToolSearchFinished(const QString &resultText,
                                              const QList<WebSearchResult> &sources,
                                              const QString &error)
{
    // 已取消或非本轮触发：忽略迟到的结果
    if (!m_searching)
        return;
    m_searching = false;
    emit webSearchChanged();

    // 来源 chip：多轮检索时累加，供 UI 展示
    for (const WebSearchResult &source : sources) {
        QVariantMap entry;
        entry.insert(QStringLiteral("title"), source.title.isEmpty() ? source.url : source.title);
        entry.insert(QStringLiteral("url"), source.url);
        m_webSources.append(entry);
    }
    emit webSearchSourcesChanged();

    if (!m_toolLoop) {
        // 循环已结束（如被取消）：仅收尾
        setBusy(false);
        return;
    }

    // 回填 tool 结果：成功即资料文本，失败也**照常继续**（让模型据此决定如何作答）
    ChatMessage toolMessage;
    toolMessage.role = ChatMessage::Tool;
    toolMessage.toolCallId = m_currentToolCallId;
    if (error.isEmpty() && !resultText.isEmpty())
        toolMessage.text = resultText;
    else
        toolMessage.text = QStringLiteral("联网搜索失败：%1")
                               .arg(error.isEmpty() ? QStringLiteral("未获取到资料") : error);
    m_loopRequest.messages.append(toolMessage);
    runNextToolCall();
}

void ChatController::finishAssistantRound(const ChatResponse &response)
{
    if (!response.text.isEmpty()) {
        m_replyText = response.text;
        emit replyTextChanged();
    }
    // 以整包文本对齐流式累积结果，避免分片丢失导致内容不一致
    const qint64 elapsed = m_roundTimer.isValid() ? m_roundTimer.elapsed() : -1;
    m_roundTimer.invalidate();
    if (m_history && m_history->available()) {
        m_history->finalizeAssistantMessage(QStringLiteral("complete"), m_replyText,
                                            response.usage, elapsed);
    }

    // 来源落库：citations 关联到本轮 assistant 消息（落库失败不影响对话）
    persistCitations(m_pendingAssistantMessageId);
    m_pendingAssistantMessageId = 0;

    setBusy(false);
}

QString ChatController::toolQueryFrom(const QString &arguments)
{
    const QString trimmed = arguments.trimmed();
    if (trimmed.isEmpty())
        return QString();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject object = document.object();
        const QJsonValue query = object.value(QStringLiteral("query"));
        if (query.isString() && !query.toString().trimmed().isEmpty())
            return query.toString().trimmed();
        // 参数名不标准时：取第一个非空字符串值兜底
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.value().isString() && !it.value().toString().trimmed().isEmpty())
                return it.value().toString().trimmed();
        }
        return QString();
    }
    // 非 JSON：容错把裸关键词当作 query（部分模型会直接给出字符串）
    if (!trimmed.contains(QLatin1Char('{')))
        return trimmed;
    return QString();
}

void ChatController::sendRequest(const ChatRequest &request)
{
    if (!m_provider) {
        setLastError(QStringLiteral("Provider 不可用"));
        return;
    }

    ChatRequest effective = request;
    effective.model = request.model.isEmpty() ? m_modelId : request.model;
    // 图片是否可上传由用户的显式确认开关决定（见 docs/AGENT.md 硬性约束）
    effective.imagesConfirmed = m_imagesConfirmed;

    m_replyText.clear();
    emit replyTextChanged();
    setLastError(QString());
    setBusy(true);
    // 一轮从「首个请求发出」计时；工具循环的后续请求不重置，保证耗时覆盖整轮
    if (!m_roundTimer.isValid())
        m_roundTimer.start();
    m_provider->sendChat(effective);
}

void ChatController::cancel()
{
    // 资料库检索阶段取消：整轮作废（不再发送对话请求），迟到结果被丢弃
    if (m_libraryRetrieving) {
        m_libraryRetrieving = false;
        if (m_retriever)
            m_retriever->cancel();
        setBusy(false);
        m_libraryStatus = QStringLiteral("已取消资料库检索");
        emit libraryChanged();
        return;
    }
    // 检索阶段取消：整轮作废（不再发送对话请求），并丢弃迟到的检索结果
    if (m_searching) {
        m_searching = false;
        if (m_webTool)
            m_webTool->cancel();
        m_toolLoop = false;
        m_toolQueue.clear();
        m_toolQueueIndex = 0;
        setBusy(false);
        emit webSearchChanged();
        return;
    }
    // 生成阶段取消：终止工具循环，避免半途状态残留
    if (m_toolLoop) {
        m_toolLoop = false;
        m_toolQueue.clear();
        m_toolQueueIndex = 0;
    }
    if (m_provider && m_busy)
        m_provider->cancel();
}

void ChatController::regenerateLast()
{
    // 忙碌时忽略：不打断在途请求，也不留下半轮状态
    if (m_busy || m_libraryRetrieving || m_searching)
        return;
    if (!m_history || !m_history->available()) {
        setLastError(QStringLiteral("会话不可用，无法重新生成"));
        return;
    }

    const QString text = m_history->lastUserMessageText();
    if (text.trimmed().isEmpty()) {
        setLastError(QStringLiteral("没有可重新生成的消息"));
        return;
    }

    // 删除最后一条助手消息（含错误消息），再复用最后一条用户消息重发
    m_history->removeLastAssistantMessage();

    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;
    dispatchUserMessage(message, false);
}

QString ChatController::renderMarkdown(const QString &text) const
{
    if (text.trimmed().isEmpty())
        return QString();
    // Qt 自带 Markdown 导入器：不执行脚本、不加载外部资源，输出 RichText 可用 HTML
    return QTextDocumentFragment::fromMarkdown(text).toHtml();
}

void ChatController::ensureProvider()
{
    if (m_provider)
        return;
    rebuildProvider();
}

void ChatController::setCredential(CredentialStatus status, const QString &message)
{
    if (m_credentialStatus == status && m_credentialMessage == message)
        return;
    m_credentialStatus = status;
    m_credentialMessage = message;
    emit credentialChanged();
}

void ChatController::setValidating(bool validating)
{
    if (m_validating == validating)
        return;
    m_validating = validating;
    emit credentialChanged();
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
