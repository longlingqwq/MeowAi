#include "core/update/UpdateManager.h"

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
{
}

void UpdateManager::checkForUpdates()
{
    emit checkFailed(QStringLiteral("更新检查尚未实现（阶段三接入）"));
}
