#pragma once

#include "storage/LibraryStore.h"

#include <QList>
#include <QString>

class LibraryManager;

/**
 * 小库（Collection）管理（core 层，功能 #1）。
 *
 * 只做小库本身与「文件—小库」关系的编排，不碰磁盘文件、不碰解析与检索。
 *
 * **安全红线**（本类遵守并需被测试证明）：
 *  - 删除小库、解除关联**绝不删除用户原文件**；
 *  - 只删除索引与库内元数据：库记录、归库关系、以及因失去归属而变得无意义的那部分文件**索引记录**；
 *  - 同时属于其他小库的文件记录一律保留（只解除重复的归库关系）；
 *  - 关联文件夹（`source_kind=folder`）的源文件永远只在用户自己的目录里，从不由本模块删除。
 */
class CollectionManager
{
public:
    CollectionManager(LibraryStore *store, LibraryManager *files);

    bool isAvailable() const;

    // ---------------------------------------------------------------- 小库
    QList<CollectionRecord> collections(QString *error) const;
    bool createCollection(const QString &name, const QString &description, qint64 *newId,
                          QString *error) const;
    bool renameCollection(qint64 collectionId, const QString &name, const QString &description,
                          QString *error) const;

    /// 删除小库：先解除该库的文件夹快照（其索引记录按归属清理），再删库与归库关系。
    /// **用户磁盘上的文件（含上传导入的原文件）一个都不删**；上传副本的清理只发生在
    /// 「文件记录因失去所有归属而被移除」的路径上，由 LibraryManager 统一负责。
    bool removeCollection(qint64 collectionId, QString *error) const;

    // ------------------------------------------------------------ 归库关系
    /// 一个文件可属于多个小库；重复加入同一库是幂等的
    bool addFileToCollection(qint64 fileId, qint64 collectionId, QString *error) const;
    /// 解除关联：只删关系，文件记录与磁盘文件都保留
    bool removeFileFromCollection(qint64 fileId, qint64 collectionId, QString *error) const;
    QList<qint64> collectionsOfFile(qint64 fileId, QString *error) const;

private:
    /// 该文件是否除 `exceptCollectionId` 外不再归属任何小库
    bool belongsOnlyTo(qint64 fileId, qint64 exceptCollectionId, QString *error) const;

    LibraryStore *m_store = nullptr;
    LibraryManager *m_files = nullptr;
};
