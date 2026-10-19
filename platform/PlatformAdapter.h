#pragma once

#include <QByteArray>
#include <QString>

/**
 * 平台适配层抽象接口。
 *
 * 所有系统级差异（路径、网络可达性、密钥库、安装器）都必须收敛到此接口，
 * 上层禁止直接调用平台 API（见 docs/AGENT.md §2）。
 * 本阶段只定义契约：Windows 给出基础实现，Android / Web 为占位实现。
 */
class PlatformAdapter
{
public:
    virtual ~PlatformAdapter() = default;

    /// 平台标识："windows" / "linux" / "macos" / "android" / "web"
    virtual QString platformId() const = 0;

    /// 用户数据根目录；config/library/categories/backups/skills/assets 均置于其下
    virtual QString appDataRoot() const = 0;

    /// 缓存目录（可被系统清理，禁止存放用户数据）
    virtual QString cacheRoot() const = 0;

    /// 内置 Skill 目录（qrc 内嵌资源，形如 ":/skills"）
    virtual QString bundledSkillsRoot() const = 0;

    /// 粗略的网络可达性判断；无法判定时返回 true，交由请求层处理错误
    virtual bool isNetworkAvailable() const = 0;

    // --- 本地安全（功能 #5）：系统级加密原语，收敛平台差异；文件读写由 storage/ 负责 ---
    /// 本平台是否具备**真实**系统级加密能力（上层据此判定"是否已加密安全"，不得假设）
    virtual bool supportsSecretEncryption() const = 0;
    /// 明文 -> 密文块（Windows: DPAPI 用户作用域；Android: AndroidKeyStore + AES-GCM）
    virtual bool protectSecret(const QByteArray &plain, QByteArray *blob, QString *error) = 0;
    /// 密文块 -> 明文；密文损坏 / 非本机当前用户时返回 false 并写 error，绝不抛异常
    virtual bool unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error) = 0;

    // --- 安装器（功能 #2）：Win / Android 覆盖升级；Web 承载 iOS，不支持自更新 ---
    virtual bool supportsSelfUpdate() const = 0;
    virtual bool installUpdate(const QString &packagePath, QString *error) = 0;
};
