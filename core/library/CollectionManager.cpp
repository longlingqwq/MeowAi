#include "core/library/CollectionManager.h"

#include "core/library/LibraryManager.h"

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

}  // namespace

CollectionManager::CollectionManager(LibraryStore *store, LibraryManager *files)
    : m_store(store)
    , m_files(files)
{
}

bool CollectionManager::isAvailable() const
{
    return m_store && m_store->isAvailable();
}

QList<CollectionRecord> CollectionManager::collections(QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取小库列表"));
        return {};
    }
    return m_store->collections(error);
}

bool CollectionManager::createCollection(const QString &name, const QString &description,
                                         qint64 *newId, QString *error) const
{
    if (newId) {
        *newId = 0;
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法新建小库"));
        return false;
    }
    return m_store->insertCollection(name, description, newId, error);
}

bool CollectionManager::renameCollection(qint64 collectionId, const QString &name,
                                         const QString &description, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法修改小库"));
        return false;
    }
    return m_store->updateCollection(collectionId, name, description, error);
}

bool CollectionManager::removeCollection(qint64 collectionId, QString *error) const
{
    if (!isAvailable() || !m_files) {
        setError(error, QStringLiteral("数据库不可用，无法删除小库"));
        return false;
    }

    // ① 先解除该库下的关联文件夹：
    //    快照本身的文件记录按「是否还有其它归属」决定保留或移除；
    //    无论哪条路径，**用户磁盘上的文件都不会被删除**（folder 类只删索引记录）。
    const QList<FolderLinkRecord> links = m_store->folderLinks(collectionId, error);
    for (const FolderLinkRecord &link : links) {
        if (!m_files->unlinkFolder(link.id, error)) {
            return false;
        }
    }

    // ② 该库下剩余、且已不再归属任何小库的文件记录：移除其索引
    //    （upload 类会顺带清理**我们自己的库内副本**；folder 类只删记录，用户文件不动）
    const QList<LibraryFileRecord> files = m_store->filesInCollection(collectionId, error);
    for (const LibraryFileRecord &file : files) {
        if (belongsOnlyTo(file.id, collectionId, error)) {
            if (!m_files->removeFileRecord(file.id, error)) {
                return false;
            }
        }
    }

    // ③ 删除库记录：归库关系与外键关联的快照行由数据库级联清理
    return m_store->removeCollection(collectionId, error);
}

bool CollectionManager::addFileToCollection(qint64 fileId, qint64 collectionId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法关联小库"));
        return false;
    }
    return m_store->linkFileToCollection(fileId, collectionId, error);
}

bool CollectionManager::removeFileFromCollection(qint64 fileId, qint64 collectionId,
                                                 QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法解除关联"));
        return false;
    }
    return m_store->unlinkFileFromCollection(fileId, collectionId, error);
}

QList<qint64> CollectionManager::collectionsOfFile(qint64 fileId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取文件所属小库"));
        return {};
    }
    return m_store->collectionIdsOfFile(fileId, error);
}

bool CollectionManager::belongsOnlyTo(qint64 fileId, qint64 exceptCollectionId, QString *error) const
{
    QList<qint64> owners = m_store->collectionIdsOfFile(fileId, error);
    owners.removeAll(exceptCollectionId);
    return owners.isEmpty();
}
