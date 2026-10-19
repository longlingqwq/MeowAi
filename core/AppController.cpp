#include "core/AppController.h"

#include "core/chat/ChatController.h"
#include "core/chat/ChatHistoryModel.h"
#include "core/retriever/NullRetriever.h"
#include "core/skill/SkillRegistry.h"
#include "core/theme/ThemeManager.h"
#include "core/theme/ThemePresetManager.h"
#include "core/update/UpdateManager.h"
#include "platform/PlatformAdapter.h"
#include "platform/PlatformAdapterFactory.h"
#include "storage/AppPaths.h"
#include "storage/ChatStore.h"
#include "storage/Database.h"
#include "storage/PersonalizationStore.h"
#include "storage/SecureStore.h"
#include "storage/ThemeStore.h"

#include <QCoreApplication>
#include <QDir>

namespace {

/// 主题预设内嵌在可执行程序资源里，core 只按路径读取，不感知 qrc 细节
constexpr auto kThemePresetRoot = ":/themes";

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

ChatHistoryModel *AppController::chatHistoryModel() const
{
    return m_chatHistory.get();
}

void AppController::initialize()
{
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

    // AI 接入（功能 #3 / #4）：对话控制器，Key 只从 SecureStore 读取，历史经会话模型
    m_chat = std::make_unique<ChatController>(m_secureStore.get(), m_chatHistory.get());

    // 资料库检索接口仅预留：阶段一固定挂载空实现（不实现检索算法）
    m_retriever = std::make_unique<NullRetriever>();

    setStatusMessage(QStringLiteral("已就绪 · 平台 %1 · 内置技能 %2 个 · 密钥后端 %3 · 个性化 %4 · 会话 %5")
                         .arg(m_platform->platformId())
                         .arg(skillCount)
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
