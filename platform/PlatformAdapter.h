#pragma once

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

    // --- 本地安全（功能 #5）：转发系统 Keychain / Keystore，本阶段为占位 ---
    virtual bool writeSecret(const QString &key, const QString &value, QString *error) = 0;
    virtual bool readSecret(const QString &key, QString *value, QString *error) = 0;
    virtual bool removeSecret(const QString &key, QString *error) = 0;

    // --- 安装器（功能 #2）：Win / Android 覆盖升级；Web 承载 iOS，不支持自更新 ---
    virtual bool supportsSelfUpdate() const = 0;
    virtual bool installUpdate(const QString &packagePath, QString *error) = 0;
};
