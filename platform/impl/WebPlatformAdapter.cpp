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

bool WebPlatformAdapter::supportsCustomTitleBar() const
{
    // Web / WASM 由宿主（浏览器 / iOS 壳）提供窗口 chrome，自绘标题栏无意义
    return false;
}

bool WebPlatformAdapter::supportsSecretEncryption() const
{
    // Web 端（承载 iOS 用户）没有可用的系统级加密存储：
    // 明确返回 false，由 storage/SecureStore 拒绝写入并提示用户，绝不降级为明文落盘。
    return false;
}

bool WebPlatformAdapter::protectSecret(const QByteArray &plain, QByteArray *blob, QString *error)
{
    Q_UNUSED(plain)
    if (blob) {
        blob->clear();
    }
    return unsupported(error, QStringLiteral("本地加密存储（Web 端不提供，请改用桌面端保存 API Key）"));
}

bool WebPlatformAdapter::unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error)
{
    Q_UNUSED(blob)
    if (plain) {
        plain->clear();
    }
    return unsupported(error, QStringLiteral("本地加密存储（Web 端不提供）"));
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
