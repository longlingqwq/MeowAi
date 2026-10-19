#pragma once

#include <QString>

class PlatformAdapter;

/**
 * 敏感数据（API Key 等）本地加密保险库（功能 #5）。
 *
 * 分层：系统级加密原语由 `platform/` 提供（Windows DPAPI / Android Keystore），
 * 本类只负责保险库文件布局（`<dataRoot>/secure/<key>.vault`）、Key 名校验与
 * has / save / load / erase 语义。
 *
 * 安全契约（重要）：
 *  - **明文绝不落盘**：平台不具备真实加密能力时（如 Web 端），save 直接拒绝并返回明确错误，
 *    **绝不降级为明文**；也绝不上传。
 *  - 密文仅本机当前用户可解（DPAPI 用户作用域 / AndroidKeyStore 绑定应用）。
 *  - `isAvailable()` 只表示"**本平台具备真实加密能力**"，与"是否已存有 Key"无关；
 *    上层据此提示用户，**不得默认其"已加密安全"**。
 */
class SecureStore
{
public:
    enum class Availability {
        Available,    ///< 具备真实系统级加密能力
        Unavailable,  ///< 平台不支持（调用 save / load 一律被拒绝）
    };

    /// dataRoot 为空时回退到 platform->appDataRoot()；保险库位于 <dataRoot>/secure/
    explicit SecureStore(PlatformAdapter *platform, QString dataRoot = QString());

    Availability availability() const;

    /// 语义修正：仅表示"本平台具备真实加密能力"，不代表已存在可用 Key
    bool isAvailable() const;
    /// 当前加密后端名称，用于向用户明示（如 "Windows DPAPI" / "不可用"）
    QString backendName() const;

    /// KEY 是否存在密文（存在不代表一定能解开，需 load 校验）
    bool has(const QString &key) const;

    bool save(const QString &key, const QString &value, QString *error);
    bool load(const QString &key, QString *value, QString *error);
    /// 幂等：目标不存在时视为已删除，返回 true
    bool erase(const QString &key, QString *error);

    /// 多 Provider 各自独立 Key 的命名规则：provider.<providerId>.api_key
    static QString providerApiKeyId(const QString &providerId);

private:
    QString dataRoot() const;
    QString vaultDir() const;
    QString vaultPath(const QString &key) const;
    bool validateKey(const QString &key, QString *error) const;

    PlatformAdapter *m_platform = nullptr;
    QString m_dataRoot;
};
