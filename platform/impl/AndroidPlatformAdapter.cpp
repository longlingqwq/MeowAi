#include "platform/impl/AndroidPlatformAdapter.h"

#include <QDir>
#include <QStandardPaths>

namespace {

bool notImplemented(QString *error, const QString &what)
{
    if (error) {
        *error = QStringLiteral("Android 平台层 %1 尚未实现（阶段四接入）").arg(what);
    }
    return false;
}

} // namespace

QString AndroidPlatformAdapter::platformId() const
{
    return QStringLiteral("android");
}

QString AndroidPlatformAdapter::appDataRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return path.isEmpty() ? QDir::homePath() + QStringLiteral("/.meowai") : path;
}

QString AndroidPlatformAdapter::cacheRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? appDataRoot() + QStringLiteral("/cache") : path;
}

QString AndroidPlatformAdapter::bundledSkillsRoot() const
{
    return QStringLiteral(":/skills");
}

bool AndroidPlatformAdapter::isNetworkAvailable() const
{
    return true;
}

bool AndroidPlatformAdapter::writeSecret(const QString &key, const QString &value, QString *error)
{
    Q_UNUSED(key)
    Q_UNUSED(value)
    return notImplemented(error, QStringLiteral("Keystore 写入"));
}

bool AndroidPlatformAdapter::readSecret(const QString &key, QString *value, QString *error)
{
    Q_UNUSED(key)
    if (value) {
        value->clear();
    }
    return notImplemented(error, QStringLiteral("Keystore 读取"));
}

bool AndroidPlatformAdapter::removeSecret(const QString &key, QString *error)
{
    Q_UNUSED(key)
    return notImplemented(error, QStringLiteral("Keystore 删除"));
}

bool AndroidPlatformAdapter::supportsSelfUpdate() const
{
    return true;
}

bool AndroidPlatformAdapter::installUpdate(const QString &packagePath, QString *error)
{
    Q_UNUSED(packagePath)
    return notImplemented(error, QStringLiteral("Apk 覆盖安装"));
}
