#pragma once

#include "core/retriever/RetrievalConfig.h"
#include "core/web/WebSearchProvider.h"
#include "providers/AiProvider.h"
#include "storage/AiConfigStore.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class ChatHistoryModel;
class LibraryConfigStore;
class LibraryStore;
class NetworkTransport;
class ProviderCatalog;
class RagRetriever;
class SecureStore;
class WebConfigStore;
class WebSearchService;
class WebSearchTool;

/**
 * 对话控制器（core 层，功能 #3 / #4）。
 *
 * 职责边界：
 *  - API Key 只从 storage/SecureStore 读取，**绝不写入配置或日志**；
 *  - 厂商 / 模型 / 思考档位等选择只经 storage/AiConfigStore 持久化，本类不直接写文件；
 *  - 厂商与模型元数据只从 core/ai/ProviderCatalog 读取（UI 只绑定本类的 QML 视图）；
 *  - 组装 ChatRequest 并交给 providers/ 层发送，自身不含任何网络 / 协议细节；
 *  - 图片上传必须经用户**显式确认**（imagesConfirmed），否则由 Provider 拒绝。
 *
 * 数据驱动：新增厂商 / 模型＝改 resources/providers.json，本类无需改动。
 */
class ChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString providerId READ providerId NOTIFY providerChanged)
    Q_PROPERTY(QString modelId READ modelId WRITE setModelId NOTIFY modelChanged)
    Q_PROPERTY(QString thinkingLevel READ thinkingLevel WRITE setThinkingLevel NOTIFY thinkingChanged)
    Q_PROPERTY(QStringList thinkingLevels READ thinkingLevels NOTIFY thinkingChanged)
    Q_PROPERTY(QString credentialStatus READ credentialStatus NOTIFY credentialChanged)
    Q_PROPERTY(QString credentialMessage READ credentialMessage NOTIFY credentialChanged)
    Q_PROPERTY(bool validating READ validating NOTIFY credentialChanged)
    Q_PROPERTY(bool customizable READ customizable NOTIFY providerChanged)
    Q_PROPERTY(QString customBaseUrl READ customBaseUrl WRITE setCustomBaseUrl NOTIFY providerChanged)
    Q_PROPERTY(bool modelListFetchable READ modelListFetchable NOTIFY providerChanged)
    Q_PROPERTY(QString modelListMessage READ modelListMessage NOTIFY modelListChanged)
    Q_PROPERTY(bool supportsVision READ supportsVision NOTIFY modelChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString replyText READ replyText NOTIFY replyTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY apiKeyStateChanged)
    Q_PROPERTY(bool dataStoreAvailable READ dataStoreAvailable NOTIFY apiKeyStateChanged)
    Q_PROPERTY(bool imagesConfirmed READ imagesConfirmed WRITE setImagesConfirmed NOTIFY imagesConfirmedChanged)
    Q_PROPERTY(bool webSearchEnabled READ webSearchEnabled WRITE setWebSearchEnabled NOTIFY webSearchChanged)
    Q_PROPERTY(QString webSearchEndpoint READ webSearchEndpoint WRITE setWebSearchEndpoint NOTIFY webSearchChanged)
    Q_PROPERTY(bool webSearchReady READ webSearchReady NOTIFY webSearchChanged)
    Q_PROPERTY(bool webSearchNative READ webSearchNative NOTIFY webSearchChanged)
    Q_PROPERTY(bool webSearchSearching READ webSearchSearching NOTIFY webSearchChanged)
    Q_PROPERTY(QString webSearchMessage READ webSearchMessage NOTIFY webSearchChanged)
    Q_PROPERTY(bool hasWebSearchKey READ hasWebSearchKey NOTIFY webSearchChanged)
    Q_PROPERTY(QVariantList webSearchSources READ webSearchSources NOTIFY webSearchSourcesChanged)
    Q_PROPERTY(QVariantList mountedCollections READ mountedCollections NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryMounted READ libraryMounted NOTIFY libraryChanged)
    Q_PROPERTY(int libraryHitCount READ libraryHitCount NOTIFY libraryChanged)
    Q_PROPERTY(int libraryTokenCount READ libraryTokenCount NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryDegraded READ libraryDegraded NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryStatus READ libraryStatus NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList librarySources READ librarySources NOTIFY librarySourcesChanged)

public:
    /**
     * catalog / configStore 允许为 nullptr：此时退化为阶段二的单厂商行为，
     * history 为空时同样退化为单条消息请求（保持既有调用方兼容）。
     * webConfigStore / webSearch 允许为 nullptr：此时联网检索不可用（开关无效）。
     * providerTransport 允许为 nullptr：生产走真实网络；测试注入假实现以离线覆盖工具循环。
     */
    explicit ChatController(SecureStore *store, ChatHistoryModel *history = nullptr,
                            const ProviderCatalog *catalog = nullptr,
                            AiConfigStore *configStore = nullptr,
                            WebConfigStore *webConfigStore = nullptr,
                            WebSearchService *webSearch = nullptr,
                            NetworkTransport *providerTransport = nullptr, QObject *parent = nullptr);
    ~ChatController() override;

    QString providerId() const;
    QString modelId() const;
    QString thinkingLevel() const;
    QStringList thinkingLevels() const;
    QString credentialStatus() const;
    QString credentialMessage() const;
    bool validating() const;
    bool customizable() const;
    QString customBaseUrl() const;
    bool modelListFetchable() const;
    QString modelListMessage() const;
    bool supportsVision() const;
    bool busy() const;
    QString replyText() const;
    QString lastError() const;
    bool hasApiKey() const;
    bool dataStoreAvailable() const;
    bool imagesConfirmed() const;
    void setImagesConfirmed(bool confirmed);

    // ---- 联网检索（v0.2.4；B 形态：模型自主按需联网，工具循环在 core 层）----
    bool webSearchEnabled() const;
    void setWebSearchEnabled(bool enabled);
    /// 自定义搜索源端点（仅当当前厂商**没有**内置搜索时使用；第三级菜单可编辑）
    QString webSearchEndpoint() const;
    void setWebSearchEndpoint(const QString &endpoint);
    /// 当前厂商是否具备**内置**搜索能力（具备即可零配置、复用厂商 API Key）
    bool webSearchNative() const;
    /// 是否正在联网检索（驱动「正在联网搜索…」状态）
    bool webSearchSearching() const;
    /// 是否已具备检索条件（内置源复用厂商 Key，或自定义源端点 + 搜索密钥齐备）
    bool webSearchReady() const;
    /// 检索配置状态说明；空串表示就绪，否则为需展示的中文提示
    QString webSearchMessage() const;
    bool hasWebSearchKey() const;
    /// 保存自定义搜索密钥（只写 SecureStore，绝不落明文）
    Q_INVOKABLE bool saveWebSearchKey(const QString &apiKey);
    Q_INVOKABLE bool eraseWebSearchKey();
    /// 上一轮联网检索命中的来源：[{title, url}]，供 UI 展示来源 chip
    QVariantList webSearchSources() const;

    // ---- 资料库（A 形态：挂载小库 → 应用侧预检索 → 注入 prompt）----
    /**
     * 装配资料库（均可为 nullptr：为空即保持原有纯对话行为）。
     * store 用于 citations 落库；retriever 负责检索；configStore 记忆挂载的小库。
     */
    void attachLibrary(LibraryStore *store, RagRetriever *retriever,
                       LibraryConfigStore *configStore);
    /// 已挂载小库 id 列表（QML 直接绑定；空列表 = 未挂载）
    QVariantList mountedCollections() const;
    bool libraryMounted() const;
    int libraryHitCount() const;
    int libraryTokenCount() const;
    bool libraryDegraded() const;
    QString libraryStatus() const;
    /// 上一轮资料库命中的来源 chip：文件名 + 定位串 + 摘要
    QVariantList librarySources() const;
    Q_INVOKABLE void setMountedCollections(const QVariantList &collectionIds);
    Q_INVOKABLE void clearMountedCollections();

    // ---- QML 视图：厂商 / 模型 / 参数（UI 只绑定这些，不自行解析 JSON）----
    /// 厂商下拉数据：[{id, displayName, customizable, modelCount, ...}]
    Q_INVOKABLE QVariantList providerOptions() const;
    /// 模型下拉数据：内置精选清单 + 本次动态拉取到的额外模型（后者无档位 / 视觉声明）
    Q_INVOKABLE QVariantList modelOptions(const QString &providerId) const;
    /// 指定厂商下某模型的元数据（supportsVision / levels / native）；无效模型返回空表
    Q_INVOKABLE QVariantMap modelInfo(const QString &providerId, const QString &modelId) const;
    /// 当前生效的原生参数（高级覆盖优先，否则由档位映射得到）
    Q_INVOKABLE QVariantMap effectiveParams() const;
    /// 当前选择的高级覆盖；未设置时为空表
    Q_INVOKABLE QVariantMap advancedParams() const;

    // ---- 选择与配置 ----
    Q_INVOKABLE void setProvider(const QString &providerId);
    /// 属性写入入口（同时供 QML 直接调用）
    Q_INVOKABLE void setModelId(const QString &modelId);
    Q_INVOKABLE void setThinkingLevel(const QString &level);
    Q_INVOKABLE void setCustomBaseUrl(const QString &baseUrl);
    Q_INVOKABLE void setAdvancedParams(const QVariantMap &params);
    Q_INVOKABLE void clearAdvancedParams();

    // ---- 密钥（只走 SecureStore）----
    Q_INVOKABLE bool saveApiKey(const QString &apiKey);
    Q_INVOKABLE bool eraseApiKey();
    /// 手动触发 Key 校验（三态结论，不做自动轮询）
    Q_INVOKABLE void validateApiKey();
    /// 手动拉取厂商模型清单；失败保留内置清单，仅把原因写进 modelListMessage
    Q_INVOKABLE void fetchModelList();

    Q_INVOKABLE void send(const QString &text);
    /// 图片仅在 imagesConfirmed 为 true 时才会真正上传
    Q_INVOKABLE void sendWithImage(const QString &text, const QString &imagePath);
    Q_INVOKABLE void cancel();

signals:
    void providerChanged();
    void modelChanged();
    void thinkingChanged();
    void credentialChanged();
    void modelListChanged();
    void apiKeyStateChanged();
    void busyChanged();
    void replyTextChanged();
    void lastErrorChanged();
    void imagesConfirmedChanged();
    void webSearchChanged();
    void webSearchSourcesChanged();
    void libraryChanged();
    void librarySourcesChanged();

private:
    void applyLoadedConfig(const AiConfigData &data);
    void rebuildProvider();
    void persist();
    QString defaultProviderId() const;
    QString defaultModelId(const QString &providerId) const;
    QString defaultThinkingLevel(const QString &providerId, const QString &modelId) const;
    QString selectionKey() const;
    ProviderConfig buildProviderConfig(const QString &apiKey) const;
    void setCredential(CredentialStatus status, const QString &message);
    void setValidating(bool validating);
    void ensureProvider();
    /// 组装一轮请求：写入会话历史（可用时）、按有界上下文补全 messages，再交给 Provider
    void dispatchUserMessage(const ChatMessage &userMessage);
    /// 资料库检索完成后的续跑：装配请求并发送（libraryContext 为空则按普通对话）
    void continueDispatch(const ChatMessage &userMessage, const QString &apiKey,
                          const QString &libraryContext);
    /// 发起资料库预检索（异步）；未挂载 / 未装配时不会进入此路径
    void beginLibraryRetrieval(const ChatMessage &userMessage);
    /// 检索前重新读取资料库配置：用户可能在本次会话里才配好 embedding
    void syncRetrievalConfig();
    void handleLibraryRetrievalFinished(const RetrievalResult &result);
    void handleLibraryRetrievalFailed(const ChatError &error);
    /// 把命中片段组装成注入 prompt 的系统消息（含「必须列出引用来源」约束）
    static QString buildLibraryContext(const RetrievalResult &result);
    /// 保存 / 读取挂载的小库（config.json 的 library 段）
    void persistMountedCollections();
    /// 本轮回答完成后把来源写入 citations（无 messageId / store 时静默跳过）
    void persistCitations(qint64 messageId);

    /// 依当前厂商解析「生效的检索方案」：内置源（复用厂商 Key）优先，其次自定义搜索源。
    /// supported 回传厂商是否支持联网；notice 回传不支持 / 未就绪时的中文提示。
    WebSearchConfig effectiveSearchConfig(bool *supported, QString *notice) const;
    /// 工具循环：本轮 Provider 回复到达（可能要求调用工具）
    void handleToolLoopFinished(const ChatResponse &response);
    /// 工具循环：依次执行队列中的工具调用（全部执行完后发下一轮请求）
    void runNextToolCall();
    /// 工具循环：一次联网检索结束（回填 tool 结果后继续）
    void handleToolSearchFinished(const QString &resultText, const QList<WebSearchResult> &sources,
                                  const QString &error);
    /// 收尾一轮助手回答：对齐文本、落库、结束忙碌态
    void finishAssistantRound(const ChatResponse &response);
    /// 解析模型给出的工具参数 JSON，取出 query 字段（容错：裸字符串按 query 处理）
    static QString toolQueryFrom(const QString &arguments);

    void persistWebConfig();
    void sendRequest(const ChatRequest &request);
    void setBusy(bool busy);
    void setLastError(const QString &message);

    SecureStore *m_store = nullptr;
    ChatHistoryModel *m_history = nullptr;
    const ProviderCatalog *m_catalog = nullptr;
    AiConfigStore *m_configStore = nullptr;
    WebConfigStore *m_webConfigStore = nullptr;
    WebSearchService *m_web = nullptr;
    /// 仅用于构造 Provider：生产为 nullptr（Provider 自建真实网络），测试注入假传输
    NetworkTransport *m_providerTransport = nullptr;

    std::unique_ptr<AiProvider> m_provider;
    AiConfigData m_config;

    QString m_providerId;
    QString m_modelId;
    QString m_thinkingLevel;
    /// 会话内记忆：每个厂商上次选择的模型（不落盘，避免配置段膨胀）
    QHash<QString, QString> m_modelByProvider;
    /// 动态拉取到的模型清单：providerId -> 模型 id 列表（仅内存，失败不清空）
    QHash<QString, QStringList> m_fetchedModels;
    QString m_modelListMessage;

    CredentialStatus m_credentialStatus = CredentialStatus::Unknown;
    QString m_credentialMessage;
    bool m_validating = false;

    bool m_busy = false;
    QString m_replyText;
    QString m_lastError;
    bool m_imagesConfirmed = false;

    /// 联网检索（B 形态）：开关 / 自定义端点记忆在 config.json 的 web 段
    bool m_webEnabled = false;
    QString m_webEndpoint;
    /// 把检索能力包装成模型工具的适配器（m_web 为空时不创建）
    std::unique_ptr<WebSearchTool> m_webTool;
    /// 检索进行中：为 true 时禁止并发提问，且只在此时接受检索结果
    bool m_searching = false;
    /// 工具循环进行中：模型可多轮调用工具（受 kMaxToolRounds 限制）
    bool m_toolLoop = false;
    /// 已完成的工具轮数（防死循环）
    int m_toolRounds = 0;
    /// 工具循环期间逐轮增长的请求（含 tools 与已追加的 assistant/tool 消息）
    ChatRequest m_loopRequest;
    /// 本轮待执行的工具调用队列（模型可能一次要求多个）
    QList<ChatToolCall> m_toolQueue;
    int m_toolQueueIndex = 0;
    /// 当前正在执行的工具调用 id（回填 tool 结果时必须原样带回）
    QString m_currentToolCallId;
    /// 上一轮命中的来源快照（供 UI 来源 chip）
    QVariantList m_webSources;

    // ---- 资料库（A 形态）----
    LibraryStore *m_libraryStore = nullptr;
    RagRetriever *m_retriever = nullptr;
    LibraryConfigStore *m_libraryConfigStore = nullptr;
    QList<qint64> m_mountedCollectionIds;
    /// 检索进行中：只在此时接受检索回调，避免迟到结果串到下一轮
    bool m_libraryRetrieving = false;
    ChatMessage m_pendingUserMessage;
    /// 上一轮检索结果（来源 chip / token 展示 / citations 落库共用）
    RetrievalResult m_lastRetrieval;
    QString m_libraryStatus;
    /// 本轮 assistant 消息 id（citations 外键；会话不可持久化时为 0）
    qint64 m_pendingAssistantMessageId = 0;
};
