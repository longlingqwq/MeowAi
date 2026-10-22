#include "core/AppController.h"

#include "core/ai/ProviderCatalog.h"
#include "core/chat/ChatController.h"
#include "core/chat/ChatHistoryModel.h"
#include "core/library/LibraryController.h"
#include "core/retriever/EmbeddingService.h"
#include "core/retriever/FileLevelRetriever.h"
#include "core/retriever/FullTextAssembler.h"
#include "core/retriever/InjectionPlanner.h"
#include "core/retriever/NullRetriever.h"
#include "core/retriever/RagRetriever.h"
#include "core/retriever/SectionPicker.h"
#include "core/skill/SkillRegistry.h"
#include "core/theme/ThemeManager.h"
#include "core/theme/ThemePresetManager.h"
#include "core/update/UpdateManager.h"
#include "core/web/HttpWebSearchProvider.h"
#include "core/web/WebSearchService.h"
#include "platform/PlatformAdapter.h"
#include "platform/PlatformAdapterFactory.h"
#include "storage/AiConfigStore.h"
#include "storage/AppPaths.h"
#include "storage/ChatStore.h"
#include "storage/Database.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryConfigStore.h"
#include "storage/LibraryStore.h"
#include "storage/PersonalizationStore.h"
#include "storage/SecureStore.h"
#include "storage/SqliteVectorIndex.h"
#include "storage/ThemeStore.h"
#include "storage/WebConfigStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>

namespace {

/// 主题预设内嵌在可执行程序资源里，core 只按路径读取，不感知 qrc 细节
constexpr auto kThemePresetRoot = ":/themes";

/// 开发环境向上找仓库根的最大层数（构建目录通常位于 build/ 或 build/Debug/）
constexpr int kDevRootSearchDepth = 5;

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_platform(PlatformAdapterFactory::instance())
    , m_updateManager(new UpdateManager(this))
{
}

AppController::~AppController() = default;

QString AppController::appName() const
{
    return QStringLiteral("MeowAi");
}

QString AppController::platformId() const
{
    return m_platform ? m_platform->platformId() : QStringLiteral("unknown");
}

bool AppController::customTitleBar() const
{
    return m_platform && m_platform->supportsCustomTitleBar();
}

QString AppController::statusMessage() const
{
    return m_statusMessage;
}

bool AppController::userGuideAvailable() const
{
    return m_userGuideAvailable;
}

QString AppController::locateUserGuide() const
{
    // 1) 安装包内：程序目录 docs/USER_GUIDE.md；2) 程序目录根 USER_GUIDE.md
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList packaged = {
        appDir.filePath(QStringLiteral("docs/USER_GUIDE.md")),
        appDir.filePath(QStringLiteral("USER_GUIDE.md")),
    };
    for (const QString &candidate : packaged) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    // 3) 开发环境：从可执行文件目录向上找同时含 CMakeLists.txt 与 docs/USER_GUIDE.md 的仓库根
    QDir cursor(appDir);
    for (int level = 0; level < kDevRootSearchDepth; ++level) {
        if (!cursor.cdUp()) {
            break;
        }
        if (!QFileInfo::exists(cursor.filePath(QStringLiteral("CMakeLists.txt")))) {
            continue;
        }
        const QString candidate = cursor.filePath(QStringLiteral("docs/USER_GUIDE.md"));
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

bool AppController::openUserGuide()
{
    const QString path = locateUserGuide();
    const bool available = !path.isEmpty();
    if (available != m_userGuideAvailable) {
        m_userGuideAvailable = available;
        emit userGuideAvailabilityChanged();
    }
    if (!available) {
        return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString AppController::avatarCropTempUrl()
{
    // 只提供临时落点，不做图像处理：QML 渲染 512×512 PNG 后交给 Personalize.setAvatar
    const QString fileName = QStringLiteral("meowai-avatar-%1-%2.png")
                                 .arg(QDateTime::currentMSecsSinceEpoch())
                                 .arg(++m_avatarCropSeq);
    return QUrl::fromLocalFile(QDir(QDir::tempPath()).filePath(fileName)).toString();
}

ThemeManager *AppController::themeManager() const
{
    return m_themeManager;
}

ThemePresetManager *AppController::themePresetManager() const
{
    return m_themePresets;
}

UpdateManager *AppController::updateManager() const
{
    return m_updateManager;
}

PersonalizationStore *AppController::personalizationStore() const
{
    return m_personalizationStore.get();
}

SecureStore *AppController::secureStore() const
{
    return m_secureStore.get();
}

ChatController *AppController::chatController() const
{
    return m_chat.get();
}

ProviderCatalog *AppController::providerCatalog() const
{
    return m_providerCatalog.get();
}

ChatHistoryModel *AppController::chatHistoryModel() const
{
    return m_chatHistory.get();
}

LibraryStore *AppController::libraryStore() const
{
    return m_libraryStore.get();
}

LibraryConfigStore *AppController::libraryConfigStore() const
{
    return m_libraryConfigStore.get();
}

EmbeddingService *AppController::embeddingService() const
{
    return m_embeddingService.get();
}

SqliteVectorIndex *AppController::vectorIndex() const
{
    return m_vectorIndex.get();
}

RagRetriever *AppController::ragRetriever() const
{
    return m_ragRetriever.get();
}

LibraryController *AppController::libraryController() const
{
    return m_libraryController.get();
}

void AppController::initialize()
{
    // 手册可用性：启动时检测一次（QML 在引擎加载前读取本属性）
    m_userGuideAvailable = !locateUserGuide().isEmpty();

    if (!m_platform) {
        setStatusMessage(QStringLiteral("平台适配器不可用，应用以受限模式启动"));
        return;
    }

    m_paths = std::make_unique<AppPaths>(AppPaths::fromPlatform(*m_platform));

    // 个性化模块（功能 #11）：先装配配置中心，即使目录初始化失败也能以默认外观启动；
    // 配置只落在 config.json 的 personalization 段，不触碰 SecureStore 中的 Key。
    if (!m_personalizationStore) {
        m_personalizationStore = std::make_unique<PersonalizationStore>(m_paths->configFile());
    }
    if (!m_themeManager) {
        m_themeManager = new ThemeManager(*m_paths, m_personalizationStore.get(), this);
    }
    m_themeManager->load();

    // 主题系统：预设与所选主题落在**同一个** config.json 的 theme 段（与个性化段互不覆盖）
    if (!m_themeStore) {
        m_themeStore = std::make_unique<ThemeStore>(m_paths->configFile());
    }
    if (!m_themePresets) {
        m_themePresets = new ThemePresetManager(m_themeStore.get(), QString::fromLatin1(kThemePresetRoot),
                                                this);
    }
    m_themePresets->load();

    QString error;
    if (!m_paths->ensureLayout(&error)) {
        setStatusMessage(QStringLiteral("本地目录初始化失败：%1").arg(error));
        return;
    }

    m_database = std::make_unique<Database>();
    if (m_database->open(QDir(m_paths->root()).filePath(QStringLiteral("metadata.db")), &error)) {
        m_database->ensureSchema(&error);
    } else {
        setStatusMessage(QStringLiteral("数据库不可用：%1").arg(error));
    }

    m_skills = std::make_unique<SkillRegistry>(m_platform->bundledSkillsRoot());
    QStringList warnings;
    const int skillCount = m_skills->scan(&warnings);

    // 安全机制（功能 #5）：Key 加密保险库；无加密能力时仍可创建，但 save 会被拒绝
    m_secureStore = std::make_unique<SecureStore>(m_platform, m_paths->root());

    // 会话历史（阶段三）：复用同一 SQLite；数据库不可用时模型仍可构造，只是不可持久化
    m_chatStore = std::make_unique<ChatStore>(m_database.get());
    m_chatHistory = std::make_unique<ChatHistoryModel>(m_chatStore.get());
    m_chatHistory->reload();

    // 厂商 / 模型目录（v0.2.3）：只读内嵌资源；即便解析失败也不影响其余模块启动
    m_providerCatalog = std::make_unique<ProviderCatalog>();
    QString catalogError;
    if (!m_providerCatalog->load(&catalogError)) {
        setStatusMessage(QStringLiteral("厂商目录不可用：%1").arg(catalogError));
    } else if (!m_providerCatalog->warnings().isEmpty()) {
        // 单条厂商非法只跳过该条并记警告，不阻断启动
        setStatusMessage(QStringLiteral("厂商目录部分条目被跳过：%1")
                             .arg(m_providerCatalog->warnings().join(QStringLiteral("; "))));
    }

    // AI 接入配置（v0.2.3）：只读写 config.json 的 ai 段，Key 仍只走 SecureStore
    m_aiConfigStore = std::make_unique<AiConfigStore>(m_paths->configFile());

    // 联网检索（v0.2.3 新模块）：开关与端点记忆在 config.json 的 web 段；
    // HTTP 后端一律经 providers/NetworkTransport 发出（此处不注入，由实现自建生产传输）
    m_webConfigStore = std::make_unique<WebConfigStore>(m_paths->configFile());
    m_webSearchProvider = std::make_unique<HttpWebSearchProvider>();
    m_webSearch = std::make_unique<WebSearchService>(m_webSearchProvider.get(),
                                                     m_secureStore.get(), m_webConfigStore.get());

    // 资料库（v0.2.5）：持久化只走 storage，embedding 请求只走 providers 的 NetworkTransport
    m_libraryStore = std::make_unique<LibraryStore>(m_database.get());
    m_libraryConfigStore = std::make_unique<LibraryConfigStore>(m_paths->configFile());
    m_embeddingService = std::make_unique<EmbeddingService>();
    m_vectorIndex = std::make_unique<SqliteVectorIndex>(m_database.get());
    m_ragRetriever = std::make_unique<RagRetriever>(m_libraryStore.get(), m_vectorIndex.get(),
                                                    m_embeddingService.get());

    // 资料库（v0.2.6）：参考资料模式（文件级召回 + 三级注入）
    m_documentStore = std::make_unique<DocumentStore>(m_database.get());
    m_fullTextAssembler = std::make_unique<FullTextAssembler>(m_libraryStore.get());
    m_sectionPicker = std::make_unique<SectionPicker>(m_documentStore.get());
    m_injectionPlanner = std::make_unique<InjectionPlanner>(
        m_libraryStore.get(), m_documentStore.get(),
        m_fullTextAssembler.get(), m_sectionPicker.get());
    m_fileLevelRetriever = std::make_unique<FileLevelRetriever>(
        m_injectionPlanner.get(), m_documentStore.get());

    // AI 接入（功能 #3 / #4）：对话控制器，Key 只从 SecureStore 读取，历史经会话模型
    m_chat = std::make_unique<ChatController>(m_secureStore.get(), m_chatHistory.get(),
                                              m_providerCatalog.get(), m_aiConfigStore.get(),
                                              m_webConfigStore.get(), m_webSearch.get());
    m_chat->attachLibrary(m_libraryStore.get(), m_ragRetriever.get(),
                          m_libraryConfigStore.get(), m_fileLevelRetriever.get());

    // 资料库 QML 门面（v0.2.5 · M7 + v0.2.6 文件级召回）：由它统一配置 embedding / caption 与挂载列表
    m_libraryController = std::make_unique<LibraryController>(
        *m_paths, m_libraryStore.get(), m_libraryConfigStore.get(), m_embeddingService.get(),
        m_vectorIndex.get(), m_ragRetriever.get(), m_providerCatalog.get(), m_secureStore.get(),
        m_aiConfigStore.get(), m_documentStore.get(), this);
    connect(m_libraryController.get(), &LibraryController::mountChanged, this, [this] {
        if (!m_chat || !m_libraryController)
            return;
        QVariantList ids;
        const LibraryConfigData current = m_libraryConfigStore->load();
        for (qint64 id : current.mountedCollections)
            ids.append(id);
        m_chat->setMountedCollections(ids);
    });

    // 资料库检索接口仅预留：阶段一固定挂载空实现（不实现检索算法）
    m_retriever = std::make_unique<NullRetriever>();

    setStatusMessage(
        QStringLiteral("已就绪 · 平台 %1 · 内置技能 %2 个 · 厂商 %3 家 · 密钥后端 %4 · 个性化 %5 · 会话 %6")
            .arg(m_platform->platformId())
            .arg(skillCount)
            .arg(m_providerCatalog ? m_providerCatalog->providerIds().size() : 0)
            .arg(m_secureStore->backendName())
            .arg(m_themeManager && m_themeManager->available() ? QStringLiteral("可用")
                                                               : QStringLiteral("不可用"))
            .arg(m_chatHistory && m_chatHistory->available() ? QStringLiteral("可用")
                                                             : QStringLiteral("不可用")));
}

void AppController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}
