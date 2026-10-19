#include "platform/impl/DesktopPlatformAdapter.h"

#include <QDir>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#include <windows.h>

#include <dpapi.h>
#include <wincrypt.h>
#endif

namespace {

/// 统一的“未实现”应答：保证调用方拿到明确原因，而不是崩溃或静默失败
bool notImplemented(QString *error, const QString &what)
{
    if (error) {
        *error = QStringLiteral("平台层 %1 尚未实现（阶段二接入）").arg(what);
    }
    return false;
}

#if defined(Q_OS_WIN)
/// 应用绑定的附加熵：使密文即使被拷到其他应用也无法解开
QByteArray secretEntropy()
{
    return QByteArray("MeowAi.Security.v1");
}
#endif

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

bool DesktopPlatformAdapter::supportsSecretEncryption() const
{
#if defined(Q_OS_WIN)
    return true; // Windows：DPAPI 用户作用域
#else
    // Linux / macOS 需接 libsecret / Keychain，当前不具备真实加密能力
    return false;
#endif
}

bool DesktopPlatformAdapter::protectSecret(const QByteArray &plain, QByteArray *blob, QString *error)
{
    if (!blob) {
        if (error) {
            *error = QStringLiteral("内部错误：输出缓冲区为空");
        }
        return false;
    }
    blob->clear();

    if (plain.isEmpty()) {
        if (error) {
            *error = QStringLiteral("待加密内容为空");
        }
        return false;
    }

#if defined(Q_OS_WIN)
    QByteArray mutablePlain = plain; // DPAPI 需要可写内存
    QByteArray entropy = secretEntropy();

    DATA_BLOB in{static_cast<DWORD>(mutablePlain.size()), reinterpret_cast<BYTE *>(mutablePlain.data())};
    DATA_BLOB ent{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB out{};

    if (!CryptProtectData(&in, L"MeowAi secret", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (error) {
            *error = QStringLiteral("系统加密失败（错误码 %1）").arg(GetLastError());
        }
        return false;
    }

    *blob = QByteArray(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return true;
#else
    Q_UNUSED(plain)
    return notImplemented(error, QStringLiteral("系统加密（Linux / macOS 凭据库待接入）"));
#endif
}

bool DesktopPlatformAdapter::unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error)
{
    if (!plain) {
        if (error) {
            *error = QStringLiteral("内部错误：输出缓冲区为空");
        }
        return false;
    }
    plain->clear();

    if (blob.isEmpty()) {
        if (error) {
            *error = QStringLiteral("密文为空，无法解密");
        }
        return false;
    }

#if defined(Q_OS_WIN)
    QByteArray mutableBlob = blob; // DPAPI 需要可写内存
    QByteArray entropy = secretEntropy();

    DATA_BLOB in{static_cast<DWORD>(mutableBlob.size()), reinterpret_cast<BYTE *>(mutableBlob.data())};
    DATA_BLOB ent{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB out{};
    LPWSTR description = nullptr;

    if (!CryptUnprotectData(&in, &description, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (description) {
            LocalFree(description);
        }
        if (error) {
            *error = QStringLiteral("密文解密失败：文件可能已损坏或不属于当前用户（错误码 %1）")
                         .arg(GetLastError());
        }
        return false;
    }

    if (description) {
        LocalFree(description);
    }
    *plain = QByteArray(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return true;
#else
    Q_UNUSED(blob)
    return notImplemented(error, QStringLiteral("系统解密（Linux / macOS 凭据库待接入）"));
#endif
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
