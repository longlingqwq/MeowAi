#include "core/AppController.h"

#include "core/chat/ChatController.h"
#include "core/retriever/NullRetriever.h"
#include "core/skill/SkillRegistry.h"
#include "core/theme/ThemeManager.h"
#include "core/update/UpdateManager.h"
#include "platform/PlatformAdapter.h"
#include "platform/PlatformAdapterFactory.h"
#include "storage/AppPaths.h"
#include "storage/Database.h"
#include "storage/PersonalizationStore.h"
#include "storage/SecureStore.h"

#include <QCoreApplication>
#include <QDir>

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

QString AppController::statusMessage() const
{
    return m_statusMessage;
}

ThemeManager *AppController::themeManager() const
{
    return m_themeManager;
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

    // AI 接入（功能 #3 / #4）：对话控制器，Key 只从 SecureStore 读取
    m_chat = std::make_unique<ChatController>(m_secureStore.get());

    // 资料库检索接口仅预留：阶段一固定挂载空实现（不实现检索算法）
    m_retriever = std::make_unique<NullRetriever>();

    setStatusMessage(QStringLiteral("已就绪 · 平台 %1 · 内置技能 %2 个 · 密钥后端 %3 · 个性化 %4")
                         .arg(m_platform->platformId())
                         .arg(skillCount)
                         .arg(m_secureStore->backendName())
                         .arg(m_themeManager && m_themeManager->available() ? QStringLiteral("可用")
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
