#include "platform/impl/DesktopPlatformAdapter.h"

#include <QDir>
#include <QStandardPaths>

namespace {

/// 统一的“未实现”应答：保证调用方拿到明确原因，而不是崩溃或静默失败
bool notImplemented(QString *error, const QString &what)
{
    if (error) {
        *error = QStringLiteral("平台层 %1 尚未实现（阶段二接入）").arg(what);
    }
    return false;
}

} // namespace

QString DesktopPlatformAdapter::platformId() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("desktop");
#endif
}

QString DesktopPlatformAdapter::appDataRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!path.isEmpty()) {
        return path;
    }
    // 极端情况下的兜底，避免返回空路径导致上层写文件失败
    return QDir::homePath() + QStringLiteral("/.meowai");
}

QString DesktopPlatformAdapter::cacheRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? appDataRoot() + QStringLiteral("/cache") : path;
}

QString DesktopPlatformAdapter::bundledSkillsRoot() const
{
    return QStringLiteral(":/skills");
}

bool DesktopPlatformAdapter::isNetworkAvailable() const
{
    // 占位实现：不做主动探测，网络异常一律由 providers/ 层按请求失败处理
    return true;
}

bool DesktopPlatformAdapter::writeSecret(const QString &key, const QString &value, QString *error)
{
    Q_UNUSED(key)
    Q_UNUSED(value)
    return notImplemented(error, QStringLiteral("密钥库写入"));
}

bool DesktopPlatformAdapter::readSecret(const QString &key, QString *value, QString *error)
{
    Q_UNUSED(key)
    if (value) {
        value->clear();
    }
    return notImplemented(error, QStringLiteral("密钥库读取"));
}

bool DesktopPlatformAdapter::removeSecret(const QString &key, QString *error)
{
    Q_UNUSED(key)
    return notImplemented(error, QStringLiteral("密钥库删除"));
}

bool DesktopPlatformAdapter::supportsSelfUpdate() const
{
    return true;
}

bool DesktopPlatformAdapter::installUpdate(const QString &packagePath, QString *error)
{
    Q_UNUSED(packagePath)
    return notImplemented(error, QStringLiteral("覆盖升级"));
}
