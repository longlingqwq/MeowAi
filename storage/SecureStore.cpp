#include "storage/SecureStore.h"

#include "platform/PlatformAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace {

/// 保险库子目录名（charter §6 未列出的实现细节：密文集中存放，便于整目录备份/清除）
constexpr char kVaultDirName[] = "secure";
/// 密文后缀
constexpr char kVaultSuffix[] = ".vault";

/// Key 名白名单：仅小写字母/数字/点/下划线/连字符，首字符必须是字母或数字。
/// 借此排除路径分隔符与 ".."，杜绝越权写到保险库目录之外。
const QRegularExpression &keyNamePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
    return pattern;
}

} // namespace

SecureStore::SecureStore(PlatformAdapter *platform, QString dataRoot)
    : m_platform(platform)
    , m_dataRoot(std::move(dataRoot))
{
}

SecureStore::Availability SecureStore::availability() const
{
    if (m_platform && m_platform->supportsSecretEncryption()) {
        return Availability::Available;
    }
    return Availability::Unavailable;
}

bool SecureStore::isAvailable() const
{
    return availability() == Availability::Available;
}

QString SecureStore::backendName() const
{
    if (!m_platform) {
        return QStringLiteral("不可用（平台适配器缺失）");
    }

    const QString platformId = m_platform->platformId();
    if (platformId == QStringLiteral("windows")) {
        return QStringLiteral("Windows DPAPI");
    }
    if (platformId == QStringLiteral("android")) {
        return QStringLiteral("Android Keystore");
    }
    if (platformId == QStringLiteral("web")) {
        return QStringLiteral("不可用（Web 端不提供本地加密存储）");
    }
    return QStringLiteral("不可用（%1 暂未接入系统凭据库）").arg(platformId);
}

QString SecureStore::providerApiKeyId(const QString &providerId)
{
    return QStringLiteral("provider.%1.api_key").arg(providerId);
}

QString SecureStore::webSearchApiKeyId()
{
    return QStringLiteral("web.search.api_key");
}

QString SecureStore::dataRoot() const
{
    if (!m_dataRoot.isEmpty()) {
        return m_dataRoot;
    }
    // 未显式指定时回退到平台数据根，保证上层不传参也不会写错位置
    return m_platform ? m_platform->appDataRoot() : QString();
}

QString SecureStore::vaultDir() const
{
    const QString root = dataRoot();
    if (root.isEmpty()) {
        return {};
    }
    return QDir(root).filePath(QString::fromLatin1(kVaultDirName));
}

QString SecureStore::vaultPath(const QString &key) const
{
    const QString dir = vaultDir();
    if (dir.isEmpty()) {
        return {};
    }
    return QDir(dir).filePath(key + QString::fromLatin1(kVaultSuffix));
}

bool SecureStore::validateKey(const QString &key, QString *error) const
{
    if (!m_platform) {
        if (error) {
            *error = QStringLiteral("平台适配器不可用");
        }
        return false;
    }
    if (key.isEmpty() || !keyNamePattern().match(key).hasMatch()) {
        if (error) {
            *error = QStringLiteral("非法 Key 名：仅允许 [a-z0-9._-] 且首字符为字母或数字，长度 1-64");
        }
        return false;
    }
    return true;
}

bool SecureStore::has(const QString &key) const
{
    if (!validateKey(key, nullptr)) {
        return false;
    }
    const QString path = vaultPath(key);
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.size() > 0;
}

bool SecureStore::save(const QString &key, const QString &value, QString *error)
{
    if (!validateKey(key, error)) {
        return false;
    }

    if (value.isEmpty()) {
        if (error) {
            *error = QStringLiteral("待保存内容为空，已拒绝写入（避免覆盖有效密钥）");
        }
        return false;
    }

    // 无加密能力时直接拒绝，绝不降级为明文落盘
    if (availability() != Availability::Available) {
        if (error) {
            *error = QStringLiteral("本平台不支持本地加密存储，已拒绝保存（后端：%1）").arg(backendName());
        }
        return false;
    }

    const QString dir = vaultDir();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (error) {
            *error = QStringLiteral("无法创建保险库目录：%1").arg(dir);
        }
        return false;
    }

    QByteArray blob;
    QString cryptoError;
    if (!m_platform->protectSecret(value.toUtf8(), &blob, &cryptoError)) {
        if (error) {
            *error = cryptoError.isEmpty() ? QStringLiteral("系统加密失败") : cryptoError;
        }
        return false;
    }

    // QSaveFile：写入失败不会留下半截密文文件
    QSaveFile file(vaultPath(key));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法写入密钥文件：%1").arg(file.errorString());
        }
        return false;
    }
    if (file.write(blob) != blob.size()) {
        file.cancelWriting();
        if (error) {
            *error = QStringLiteral("密钥写入不完整：%1").arg(file.errorString());
        }
        return false;
    }

    // 仅属主可读写（Windows 上为尽力而为，POSIX 上确实生效）
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("密钥文件提交失败：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

bool SecureStore::load(const QString &key, QString *value, QString *error)
{
    if (value) {
        value->clear();
    }

    if (!validateKey(key, error)) {
        return false;
    }

    const QString path = vaultPath(key);
    QFile file(path);
    if (!file.exists()) {
        if (error) {
            *error = QStringLiteral("未找到已保存的密钥：%1（可先用 has() 判断）").arg(key);
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("密钥文件不可读：%1").arg(file.errorString());
        }
        return false;
    }

    const QByteArray blob = file.readAll();
    file.close();

    if (blob.isEmpty()) {
        if (error) {
            *error = QStringLiteral("密钥文件为空或已损坏：%1").arg(key);
        }
        return false;
    }

    QByteArray plain;
    QString cryptoError;
    if (!m_platform->unprotectSecret(blob, &plain, &cryptoError)) {
        if (error) {
            *error = cryptoError.isEmpty() ? QStringLiteral("密钥解密失败（文件可能已损坏）") : cryptoError;
        }
        return false;
    }

    if (value) {
        *value = QString::fromUtf8(plain);
    }
    return true;
}

bool SecureStore::erase(const QString &key, QString *error)
{
    if (!validateKey(key, error)) {
        return false;
    }

    const QString path = vaultPath(key);
    QFile file(path);
    if (!file.exists()) {
        return true; // 幂等：本来就不存在
    }
    if (!file.remove()) {
        if (error) {
            *error = QStringLiteral("密钥删除失败：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}
