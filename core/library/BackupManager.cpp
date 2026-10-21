#include "core/library/BackupManager.h"

#include <utility>

BackupManager::BackupManager(AppPaths paths)
    : m_paths(std::move(paths))
{
}

QString BackupManager::backupBeforeModify(const QString &filePath, QString *error)
{
    Q_UNUSED(filePath)
    if (error) {
        *error = QStringLiteral("备份机制尚未实现（阶段二接入）");
    }
    return {};
}
