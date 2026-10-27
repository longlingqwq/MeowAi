#pragma once

#include "storage/RoleCardStore.h"

#include <QObject>
#include <QString>

#include <memory>

class AppPaths;
class AiConfigStore;
class ChatController;
class ChatHistoryModel;
class ChatStore;
class Database;
class DocumentRetriever;
class DocumentStore;
class EmbeddingService;
class FileLevelRetriever;
class FullTextAssembler;
class InjectionPlanner;
class LibraryConfigStore;
class LibraryController;
class LibraryStore;
class PlatformAdapter;
class PersonalizationStore;
class ProviderCatalog;
class RagRetriever;
class SectionPicker;
class SecureStore;
class SkillCatalog;
class SkillStateStore;
class SqliteVectorIndex;
class ThemeManager;
class ThemePresetManager;
class ThemeStore;
class UpdateManager;
class WebConfigStore;
class WebSearchProvider;
class WebSearchService;

/**
 * 顶层门面（AppController）：
 * UI(QML) 只与本对象交互，由它按层调用 storage / platform / providers / core 模块，
 * 保证“UI 不写业务逻辑、不直连数据库”（见 docs/AGENT.md §2）。
 */
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString platformId READ platformId CONSTANT)
    Q_PROPERTY(bool customTitleBar READ customTitleBar CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    /// 角色卡存储（M18）：初始化后可用；QML 经 App.roleCardStore 读写，不直连数据库
    Q_PROPERTY(RoleCardStore *roleCardStore READ roleCardStore CONSTANT)
    /// 用户手册是否存在于查找路径中（只读检测，不复制、不改写手册文件）
    Q_PROPERTY(bool userGuideAvailable READ userGuideAvailable NOTIFY userGuideAvailabilityChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    QString appName() const;
    QString platformId() const;
    /// 本平台是否使用无边框自绘标题栏（仅桌面端；Android / Web 回退原生 chrome）
    bool customTitleBar() const;
    QString statusMessage() const;
    bool userGuideAvailable() const;

    /// 准备本地目录、数据库与内置 Skill；任何失败都只体现在 statusMessage，不崩溃
    Q_INVOKABLE void initialize();

    /// 用系统默认程序打开《用户手册》；文件不存在时返回 false（不复制、不改写手册）
    Q_INVOKABLE bool openUserGuide();
    /// 头像裁剪结果的临时文件 file:// URL（由 QML 渲染 512×512 PNG 后写入）
    Q_INVOKABLE QString avatarCropTempUrl();

    ThemeManager *themeManager() const;
    /// 主题预设管理（功能 #11 衍生）：令牌换值、零代码换肤
    ThemePresetManager *themePresetManager() const;
    UpdateManager *updateManager() const;
    /// 个性化配置存储（功能 #11）；只读写 config.json 的 personalization 段
    PersonalizationStore *personalizationStore() const;
    /// 敏感数据保险库（功能 #5）；初始化后可用，失败时为 nullptr
    SecureStore *secureStore() const;
    /// 对话控制器（功能 #3 / #4）；初始化后可用，失败时为 nullptr
    ChatController *chatController() const;
    /// 厂商 / 模型目录（v0.2.3）；只读内嵌资源，初始化后即可用（缺失时为 nullptr）
    ProviderCatalog *providerCatalog() const;
    /// 会话历史模型（阶段三）；数据库不可用时仍可构造，只是不可持久化
    ChatHistoryModel *chatHistoryModel() const;
    /// 角色卡存储（M18）：list / get / create / update / remove / 导入导出 / 头像托管
    RoleCardStore *roleCardStore() const;

    // ---- 资料库（阶段二收尾 · v0.2.5）----
    LibraryStore *libraryStore() const;
    LibraryConfigStore *libraryConfigStore() const;
    EmbeddingService *embeddingService() const;
    SqliteVectorIndex *vectorIndex() const;
    RagRetriever *ragRetriever() const;
    /// 资料库 QML 门面（v0.2.5 · M7）：小库 / 文件 / 索引 / 配置 / 成本预估
    LibraryController *libraryController() const;
    /// 技能仓库（功能 #9 · M19）：内置 + 导入技能、启用状态、全局模式
    SkillCatalog *skillCatalog() const;

signals:
    void statusMessageChanged();
    void userGuideAvailabilityChanged();

private:
    void setStatusMessage(const QString &message);
    /// 按「程序目录 docs/ → 程序目录根 → 开发环境仓库 docs/」顺序定位手册
    QString locateUserGuide() const;

    PlatformAdapter *m_platform = nullptr;
    std::unique_ptr<AppPaths> m_paths;
    std::unique_ptr<Database> m_database;
    /// 技能（功能 #9 · M19）：状态文件 + 内置/导入合并目录
    std::unique_ptr<SkillStateStore> m_skillState;
    std::unique_ptr<SkillCatalog> m_skillCatalog;
    std::unique_ptr<DocumentRetriever> m_retriever;
    std::unique_ptr<PersonalizationStore> m_personalizationStore;
    std::unique_ptr<ThemeStore> m_themeStore;
    std::unique_ptr<SecureStore> m_secureStore;
    std::unique_ptr<ProviderCatalog> m_providerCatalog;
    std::unique_ptr<AiConfigStore> m_aiConfigStore;
    /// 联网检索（v0.2.3 新模块）：配置段 web + HTTP 后端 + 编排服务
    std::unique_ptr<WebConfigStore> m_webConfigStore;
    std::unique_ptr<WebSearchProvider> m_webSearchProvider;
    std::unique_ptr<WebSearchService> m_webSearch;
    std::unique_ptr<ChatStore> m_chatStore;
    std::unique_ptr<ChatHistoryModel> m_chatHistory;
    std::unique_ptr<ChatController> m_chat;
    /// 角色卡（M18）：同一 SQLite 的 role_cards 表 + assets/rolecards 头像副本
    std::unique_ptr<RoleCardStore> m_roleCardStore;
    /// 资料库（v0.2.5）：存储 / 配置 / 向量化 / 向量索引 / 检索
    std::unique_ptr<LibraryStore> m_libraryStore;
    std::unique_ptr<LibraryConfigStore> m_libraryConfigStore;
    std::unique_ptr<EmbeddingService> m_embeddingService;
    std::unique_ptr<SqliteVectorIndex> m_vectorIndex;
    std::unique_ptr<RagRetriever> m_ragRetriever;
    /// v0.2.6: 参考资料模式（文件级召回 + 三级注入）
    std::unique_ptr<DocumentStore> m_documentStore;
    std::unique_ptr<FullTextAssembler> m_fullTextAssembler;
    std::unique_ptr<SectionPicker> m_sectionPicker;
    std::unique_ptr<InjectionPlanner> m_injectionPlanner;
    std::unique_ptr<FileLevelRetriever> m_fileLevelRetriever;
    std::unique_ptr<LibraryController> m_libraryController;
    ThemeManager *m_themeManager = nullptr;
    ThemePresetManager *m_themePresets = nullptr;
    UpdateManager *m_updateManager = nullptr;
    QString m_statusMessage;
    /// 手册可用性缓存（初始化时检测，openUserGuide 前复检并按需发通知）
    bool m_userGuideAvailable = false;
    /// 同类裁剪临时文件的去重序号
    int m_avatarCropSeq = 0;
};
