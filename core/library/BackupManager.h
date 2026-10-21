#pragma once

#include "storage/AppPaths.h"

#include <QString>

/**
 * 备份骨架（功能 #8，红线）：AI 修改资料前，必须先把原文件复制到 backups/。
 * 阶段一只固定接口，真实复制逻辑在阶段二接入资料库写入流程时实现。
 */
class BackupManager
{
public:
    explicit BackupManager(AppPaths paths);

    /// 备份原文件；成功返回备份文件路径，阶段一未实现返回空串并写入 error
    QString backupBeforeModify(const QString &filePath, QString *error);

private:
    AppPaths m_paths;
};
