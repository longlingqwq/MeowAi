#pragma once

#include "core/library/LibraryFileKinds.h"
#include "storage/AppPaths.h"
#include "storage/LibraryStore.h"

#include <QList>
#include <QString>
#include <QStringList>

/**
 * 关联文件夹快照（手动重建）的结果统计，供上层提示用户。
 */
struct FolderSnapshotResult
{
    int added = 0;        ///< 新增的文件记录
    int unchanged = 0;    ///< 内容未变化，保留原索引状态
    int markedStale = 0;  ///< 内容变化 → 标记 `stale`，待重建索引
    int missing = 0;      ///< 记录仍在、磁盘文件已不存在 → 标记 `stale`
    int unsupported = 0;  ///< 视频等本次不支持的类别：**登记但不索引**
    int skipped = 0;      ///< 扩展名不受支持：完全不入库
};

/**
 * 资料库文件管理（core 层，功能 #1 的文件来源部分）。
 *
 * 两种来源，语义完全不同：
 *  - **上传导入**：把文件**复制**进 `<AppData>/library/`，原文件毫发无损；库内副本归本模块管理。
 *  - **关联文件夹**：只对用户自己的目录建**索引引用**，**不复制、不监视**；快照由用户手动重建。
 *
 * **安全红线**：本模块只会删除**自己复制进库内目录的副本**（`isManagedCopy()` 校验通过才删），
 * 关联文件夹里的用户文件、以及上传时的原文件**永远不删**。
 */
class LibraryManager
{
public:
    LibraryManager(AppPaths paths, LibraryStore *store);

    bool isAvailable() const;
    AppPaths paths() const;

    // ------------------------------------------------------------ 上传导入
    /// 复制入库并登记；`fileId` 回传新记录 id。原文件不移动、不删除
    bool importFile(const QString &sourcePath, qint64 *fileId, QString *error);
    /// 批量导入：逐个处理，成功的保留并进入 `fileIds`；全部失败返回 false，
    /// 部分失败也返回 false（error 中列出失败项），调用方可用 `fileIds` 继续归库
    bool importFiles(const QStringList &sourcePaths, QList<qint64> *fileIds, QString *error);

    // --------------------------------------------------- 关联文件夹（快照）
    /// 关联一个文件夹并立即做首次快照。
    /// 同一物理文件夹**不允许重复关联**（跨小库亦然）：若已存在关联则给出明确错误，
    /// 避免「一个文件记录只能挂一个快照」带来的数据歧义；重复文件可用「文件归库」加入其它小库。
    bool linkFolder(qint64 collectionId, const QString &rootPath, qint64 *linkId,
                    FolderSnapshotResult *result, QString *error);
    /// 手动重建快照：新增 / 变更 / 缺失 都只更新**记录与状态**，不删除任何磁盘文件
    bool rebuildFolder(qint64 linkId, FolderSnapshotResult *result, QString *error);
    /// 解除关联：快照文件从该小库撤出（**用户磁盘文件永不删**）；
    /// 仍归属其它小库的文件则保留记录、仅脱离本快照
    bool unlinkFolder(qint64 linkId, QString *error);

    // -------------------------------------------------------------- 记录
    /// 移除文件记录：连带清理**库内副本**（upload 类）；folder 类的用户文件绝不触碰
    bool removeFileRecord(qint64 fileId, QString *error);

    // ---------------------------------------------------------- 只读视图
    QList<LibraryFileRecord> allFiles(QString *error) const;
    QList<LibraryFileRecord> filesInCollection(qint64 collectionId, QString *error) const;
    QList<FolderLinkRecord> folderLinks(qint64 collectionId, QString *error) const;

    /// 路径是否位于资料库目录内（= 本模块自己的副本）。**删除前的唯一放行条件**
    bool isManagedCopy(const QString &path) const;

    /// 内容指纹：小文件（≤ 8MB）取 SHA-256；大文件退化为「大小 + 修改时间」代理值，
    /// 避免每次快照都为上百 MB 的文件做全量哈希
    static QString contentHash(const QString &path);

    /// 单次快照的最大文件数：超出即中止（**不做部分写入**），避免误关联整个磁盘
    static constexpr int kMaxSnapshotFiles = 5000;

private:
    struct SnapshotEntry
    {
        QString path;
        QString displayName;
        qint64 size = 0;
        qint64 mtime = 0;
        LibraryKind kind = LibraryKind::Unknown;
    };

    bool scanFolder(const QString &rootPath, QList<SnapshotEntry> *entries,
                    FolderSnapshotResult *result, QString *error) const;
    bool applySnapshot(const FolderLinkRecord &link, const QList<SnapshotEntry> &entries,
                       FolderSnapshotResult *result, QString *error) const;
    bool findFolderLink(qint64 linkId, FolderLinkRecord *out, QString *error) const;
    QString uniqueTargetPath(const QString &displayName) const;

    LibraryStore *m_store = nullptr;
    AppPaths m_paths;
};
