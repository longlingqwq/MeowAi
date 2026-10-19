#pragma once

#include <QString>

class PlatformAdapter;

/**
 * 敏感数据（API Key 等）本地加密存取接口占位（功能 #5）。
 *
 * 本阶段只固定契约：真实实现（系统 Keychain / Keystore + 本地加密）在阶段二完成，
 * 上层只需调用本接口，不感知底层方案变化。
 */
class SecureStore
{
public:
    explicit SecureStore(PlatformAdapter *platform);

    /// 当前平台是否已具备可用的加密存储
    bool isAvailable() const;

    bool save(const QString &key, const QString &value, QString *error);
    bool load(const QString &key, QString *value, QString *error);
    bool erase(const QString &key, QString *error);

private:
    PlatformAdapter *m_platform = nullptr;
};
