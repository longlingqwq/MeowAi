#include "storage/SecureStore.h"

#include "platform/PlatformAdapter.h"

SecureStore::SecureStore(PlatformAdapter *platform)
    : m_platform(platform)
{
}

bool SecureStore::isAvailable() const
{
    // 占位：密钥库能力尚未接入，阶段二实现后改由平台层上报
    return false;
}

bool SecureStore::save(const QString &key, const QString &value, QString *error)
{
    if (!m_platform) {
        if (error) {
            *error = QStringLiteral("平台适配器不可用");
        }
        return false;
    }
    return m_platform->writeSecret(key, value, error);
}

bool SecureStore::load(const QString &key, QString *value, QString *error)
{
    if (!m_platform) {
        if (error) {
            *error = QStringLiteral("平台适配器不可用");
        }
        return false;
    }
    return m_platform->readSecret(key, value, error);
}

bool SecureStore::erase(const QString &key, QString *error)
{
    if (!m_platform) {
        if (error) {
            *error = QStringLiteral("平台适配器不可用");
        }
        return false;
    }
    return m_platform->removeSecret(key, error);
}
