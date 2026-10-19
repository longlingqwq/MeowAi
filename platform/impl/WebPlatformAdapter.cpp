#include "platform/impl/WebPlatformAdapter.h"

#include <QDir>
#include <QStandardPaths>

namespace {

bool unsupported(QString *error, const QString &what)
{
    if (error) {
        *error = QStringLiteral("Web 端不支持 %1").arg(what);
    }
    return false;
}

} // namespace

QString WebPlatformAdapter::platformId() const
{
    return QStringLiteral("web");
}

QString WebPlatformAdapter::appDataRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return path.isEmpty() ? QStringLiteral("/meowai") : path;
}

QString WebPlatformAdapter::cacheRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? QStringLiteral("/meowai-cache") : path;
}

QString WebPlatformAdapter::bundledSkillsRoot() const
{
    return QStringLiteral(":/skills");
}

bool WebPlatformAdapter::isNetworkAvailable() const
{
    return true;
}

bool WebPlatformAdapter::writeSecret(const QString &key, const QString &value, QString *error)
{
    Q_UNUSED(key)
    Q_UNUSED(value)
    return unsupported(error, QStringLiteral("原生密钥库写入"));
}

bool WebPlatformAdapter::readSecret(const QString &key, QString *value, QString *error)
{
    Q_UNUSED(key)
    if (value) {
        value->clear();
    }
    return unsupported(error, QStringLiteral("原生密钥库读取"));
}

bool WebPlatformAdapter::removeSecret(const QString &key, QString *error)
{
    Q_UNUSED(key)
    return unsupported(error, QStringLiteral("原生密钥库删除"));
}

bool WebPlatformAdapter::supportsSelfUpdate() const
{
    // iOS 用户只走网页端，不产出安装包（见 docs/PROJECT_CHARTER.md D4）
    return false;
}

bool WebPlatformAdapter::installUpdate(const QString &packagePath, QString *error)
{
    Q_UNUSED(packagePath)
    return unsupported(error, QStringLiteral("覆盖升级"));
}
