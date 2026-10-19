#pragma once

#include "platform/PlatformAdapter.h"

/**
 * Android 平台适配器。
 *
 * 本地安全（功能 #5）：使用 AndroidKeyStore 中的 AES 密钥 + AES/GCM 加解密，
 * 密钥不出 TEE/Keystore，密文以 IV||密文 形式交给 storage/ 落盘。
 *
 * 注意：本仓库当前无 Android 构建环境，该实现由 `#if defined(Q_OS_ANDROID)`
 * 编译（Windows 构建中被排除），**真机验证留阶段四**。
 */
class AndroidPlatformAdapter : public PlatformAdapter
{
public:
    QString platformId() const override;
    QString appDataRoot() const override;
    QString cacheRoot() const override;
    QString bundledSkillsRoot() const override;
    bool isNetworkAvailable() const override;

    bool supportsSecretEncryption() const override;
    bool protectSecret(const QByteArray &plain, QByteArray *blob, QString *error) override;
    bool unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error) override;

    bool supportsSelfUpdate() const override;
    bool installUpdate(const QString &packagePath, QString *error) override;
};
