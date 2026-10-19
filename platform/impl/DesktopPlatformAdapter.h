#pragma once

#include "platform/PlatformAdapter.h"

/**
 * 桌面端（Windows / Linux / macOS）平台适配器。
 *
 * 本地安全（功能 #5）：
 *  - Windows：使用 DPAPI（用户作用域）加解密，密文仅当前用户可解；
 *  - Linux / macOS：系统凭据库（libsecret / Keychain）留后续阶段接入，
 *    当前明确返回"不支持"（上层因此不会误以为已加密安全，也绝不降级为明文）。
 */
class DesktopPlatformAdapter : public PlatformAdapter
{
public:
    QString platformId() const override;
    QString appDataRoot() const override;
    QString cacheRoot() const override;
    QString bundledSkillsRoot() const override;
    bool isNetworkAvailable() const override;
    bool supportsCustomTitleBar() const override;

    bool supportsSecretEncryption() const override;
    bool protectSecret(const QByteArray &plain, QByteArray *blob, QString *error) override;
    bool unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error) override;

    bool supportsSelfUpdate() const override;
    bool installUpdate(const QString &packagePath, QString *error) override;
};
