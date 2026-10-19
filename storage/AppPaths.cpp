#include "storage/AppPaths.h"

#include "platform/PlatformAdapter.h"

#include <QDir>

#include <utility>

AppPaths::AppPaths(QString root)
    : m_root(std::move(root))
{
}

AppPaths AppPaths::fromPlatform(const PlatformAdapter &adapter)
{
    return AppPaths(adapter.appDataRoot());
}

QString AppPaths::root() const
{
    return m_root;
}

QString AppPaths::configFile() const
{
    return QDir(m_root).filePath(QStringLiteral("config.json"));
}

QString AppPaths::libraryDir() const
{
    return QDir(m_root).filePath(QStringLiteral("library"));
}

QString AppPaths::categoriesDir() const
{
    return QDir(m_root).filePath(QStringLiteral("categories"));
}

QString AppPaths::backupsDir() const
{
    return QDir(m_root).filePath(QStringLiteral("backups"));
}

QString AppPaths::skillsDir() const
{
    return QDir(m_root).filePath(QStringLiteral("skills"));
}

QString AppPaths::assetsDir() const
{
    return QDir(m_root).filePath(QStringLiteral("assets"));
}

QStringList AppPaths::requiredDirectories() const
{
    return { libraryDir(), categoriesDir(), backupsDir(), skillsDir(), assetsDir() };
}

bool AppPaths::ensureLayout(QString *error) const
{
    if (m_root.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("数据根目录为空，无法初始化本地布局");
        }
        return false;
    }

    QDir dir;
    for (const QString &path : requiredDirectories()) {
        if (!dir.mkpath(path)) {
            if (error) {
                *error = QStringLiteral("无法创建目录：%1").arg(path);
            }
            return false;
        }
    }
    return true;
}
