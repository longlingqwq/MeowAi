#pragma once

#include <QList>
#include <QString>

class Database;

/**
 * 资料库存储层的数据记录（storage 层内部结构，不暴露给 UI）。
 *
 * 定位元数据（loc_*）是硬性要求：回答引用来源必须能定位到「哪个文件的第几页 /
 * 哪个工作表第几行 / 第几张图」，否则用户无法核对。
 */
struct CollectionRecord
{
    qint64 id = 0;
    QString name;
    QString description;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

/// 文件来源：upload = 复制进库内目录的副本；folder = 关联文件夹的索引引用（不复制）
struct LibraryFileRecord
{
    qint64 id = 0;
    QString displayName;
    QString sourceKind = QStringLiteral("upload");
    QString sourcePath;  ///< upload=库内副本路径 / folder=用户源文件路径
    QString originPath;  ///< upload 的原文件路径；**仅记录，绝不删除**
    qint64 folderLinkId = 0;
    QString ext;
    QString kind;  ///< text | image | video
    qint64 sizeBytes = 0;
    qint64 mtime = 0;
    QString contentHash;  ///< 重建关联文件夹时判断文件是否变化
    QString status = QStringLiteral("pending");
    QString errorText;
    int chunkCount = 0;
    qint64 indexedAt = 0;
    qint64 createdAt = 0;
};

struct FolderLinkRecord
{
    qint64 id = 0;
    qint64 collectionId = 0;
    QString rootPath;
    qint64 snapshotAt = 0;
    int fileCount = 0;
    QString status = QStringLiteral("empty");
    QString errorText;
};

struct ChunkRecord
{
    qint64 id = 0;
    qint64 fileId = 0;
    int ordinal = 0;  ///< 文件内顺序（邻居扩展依赖它连续）
    QString text;
    QString locKind;   ///< page | sheet | row | para | slide | json | image
    QString locLabel;  ///< 人可读定位串：「第 3 页」「Sheet1 第 12-18 行」「图片 #2」
    int locPage = 0;
    QString locSheet;
    int locRowFrom = 0;
    int locRowTo = 0;
    int charCount = 0;
    int tokenEst = 0;
    qint64 createdAt = 0;
};

struct CitationRecord
{
    qint64 id = 0;
    qint64 messageId = 0;
    qint64 conversationId = 0;
    qint64 chunkId = 0;
    qint64 fileId = 0;
    qint64 collectionId = 0;
    QString locLabel;
    int rank = 0;
    double score = 0.0;
    qint64 createdAt = 0;
};

/**
 * 资料库存储（storage 层）。
 *
 * 只做一件事：把 schema v2 的资料库各表读写成结构化记录。
 * **不含任何业务判断**（是否复制文件、是否重新索引、检索打几分、如何组上下文，
 * 全部在 core 层），也不做 UI 展示。所有失败路径返回 false + error，绝不抛异常。
 *
 * 红线（在本层即已守住）：
 *  - 本类只操作数据库行，**永不删除用户原文件**（`source_path` / `origin_path` 指向的
 *    真实文件不归本层管）；删除小库只删库与归库关系，删除文件记录只删索引。
 *  - 关联文件夹（folder）只建引用，不复制。
 */
class LibraryStore
{
public:
    explicit LibraryStore(Database *database);

    /// 底层数据库是否可用；不可用时全部读写返回失败
    bool isAvailable() const;
    /// 关键词索引走 FTS5 还是降级 LIKE（由 Database::ensureSchema 探测决定）
    bool ftsAvailable() const;

    // ---------------------------------------------------------------- 小库
    QList<CollectionRecord> collections(QString *error) const;
    bool collectionByName(const QString &name, CollectionRecord *out, QString *error) const;
    bool insertCollection(const QString &name, const QString &description, qint64 *newId,
                          QString *error) const;
    bool updateCollection(qint64 collectionId, const QString &name, const QString &description,
                          QString *error) const;
    /// 删除小库：只删库记录 + 归库关系（级联），**不动文件记录、不动用户原文件**
    bool removeCollection(qint64 collectionId, QString *error) const;

    // ---------------------------------------------------------------- 文件
    QList<LibraryFileRecord> allFiles(QString *error) const;
    QList<LibraryFileRecord> filesInCollection(qint64 collectionId, QString *error) const;
    /// 某个关联文件夹（快照）下的文件记录，重建快照与解除关联时使用
    QList<LibraryFileRecord> filesByFolderLink(qint64 folderLinkId, QString *error) const;
    bool fileById(qint64 fileId, LibraryFileRecord *out, QString *error) const;
    bool fileBySourcePath(const QString &sourcePath, LibraryFileRecord *out, QString *error) const;
    bool insertFile(const LibraryFileRecord &record, qint64 *newId, QString *error) const;
    /// 按 id 整行更新（读改写语义，便于 core 层组合状态变更）
    bool updateFile(const LibraryFileRecord &record, QString *error) const;
    /// 删除文件记录：连带清理片段、向量与 FTS 索引行；**不删除磁盘上的真实文件**
    bool removeFile(qint64 fileId, QString *error) const;

    // ------------------------------------------------------------ 归库关系
    /// 一个文件可属于多个小库；重复关联是幂等的（不报错）
    bool linkFileToCollection(qint64 fileId, qint64 collectionId, QString *error) const;
    bool unlinkFileFromCollection(qint64 fileId, qint64 collectionId, QString *error) const;
    QList<qint64> collectionIdsOfFile(qint64 fileId, QString *error) const;

    // ------------------------------------------------------- 关联文件夹快照
    QList<FolderLinkRecord> folderLinks(qint64 collectionId, QString *error) const;
    bool insertFolderLink(const FolderLinkRecord &record, qint64 *newId, QString *error) const;
    bool updateFolderLinkSnapshot(qint64 linkId, qint64 snapshotAt, int fileCount,
                                  const QString &status, const QString &errorText,
                                  QString *error) const;
    bool removeFolderLink(qint64 linkId, QString *error) const;

    // ---------------------------------------------------------------- 片段
    /// 用新片段整体替换某文件的旧片段（事务：清旧片段 + FTS 行 → 插新 → 回填 chunk_count）
    bool replaceChunks(qint64 fileId, const QList<ChunkRecord> &chunks, QString *error) const;
    QList<ChunkRecord> chunksOfFile(qint64 fileId, QString *error) const;
    QList<ChunkRecord> chunksByIds(const QList<qint64> &chunkIds, QString *error) const;
    bool chunkById(qint64 chunkId, ChunkRecord *out, QString *error) const;
    /// 邻居扩展：取 [ordinalFrom, ordinalTo] 闭区间内的片段，按 ordinal 升序
    QList<ChunkRecord> chunksInOrdinalRange(qint64 fileId, int ordinalFrom, int ordinalTo,
                                            QString *error) const;

    // ---------------------------------------------------- 关键词检索（兜底）
    /// 关键词检索片段 id，按相关度（FTS5 bm25）或 id 升序返回。
    /// `collectionIds` 为空表示不限库；FTS5 不可用时自动降级 LIKE 子串匹配。
    QList<qint64> searchChunkIds(const QString &query, const QList<qint64> &collectionIds, int limit,
                                 QString *error) const;

    // ---------------------------------------------------------------- 来源
    bool appendCitation(const CitationRecord &record, qint64 *newId, QString *error) const;
    QList<CitationRecord> citationsOfMessage(qint64 messageId, QString *error) const;
    /// citations 表无外键（可指向已删消息），删除消息 / 会话时必须显式清理
    bool removeCitationsOfMessage(qint64 messageId, QString *error) const;
    bool removeCitationsOfConversation(qint64 conversationId, QString *error) const;

private:
    Database *m_database = nullptr;
};
