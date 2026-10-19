#include "core/AppController.h"

#include "core/retriever/NullRetriever.h"
#include "core/skill/SkillRegistry.h"
#include "core/theme/ThemeManager.h"
#include "core/update/UpdateManager.h"
#include "platform/PlatformAdapter.h"
#include "platform/PlatformAdapterFactory.h"
#include "storage/AppPaths.h"
#include "storage/Database.h"

#include <QCoreApplication>
#include <QDir>

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_platform(PlatformAdapterFactory::instance())
    , m_themeManager(new ThemeManager(this))
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

void AppController::initialize()
{
    if (!m_platform) {
        setStatusMessage(QStringLiteral("平台适配器不可用，应用以受限模式启动"));
        return;
    }

    m_paths = std::make_unique<AppPaths>(AppPaths::fromPlatform(*m_platform));

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

    // 资料库检索接口仅预留：阶段一固定挂载空实现（不实现检索算法）
    m_retriever = std::make_unique<NullRetriever>();

    setStatusMessage(QStringLiteral("已就绪 · 平台 %1 · 内置技能 %2 个")
                         .arg(m_platform->platformId())
                         .arg(skillCount));
}

void AppController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}
