#include "storage/Database.h"
#include "storage/LibraryStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 资料库存储层测试：
 *  - 小库 / 文件 / 归库关系 / 文件夹快照 / 片段 / 关键词索引 / 引用来源的读写往返；
 *  - **安全红线**：删除小库与解除关联不得删除文件记录，更不得删除磁盘上的真实文件；
 *  - 异常流（无数据库、名称重复、来源路径重复）不得崩溃。
 */
class TstLibraryStore : public QObject
{
    Q_OBJECT

private slots:
    void unavailableWithoutDatabase();
    void collectionCrud();
    void duplicateCollectionNameRejected();
    void removingCollectionKeepsFilesAndOtherMembership();
    void fileMembershipIsManyToManyAndIdempotent();
    void duplicateSourcePathRejected();
    void fileRoundTripAndUpdate();
    void replaceChunksIsAtomicAndIdempotent();
    void neighborRangeAndChunkById();
    void keywordSearchRespectsCollectionFilterAndLimit();
    void removingFileRecordNeverTouchesDiskFile();
    void folderLinkSnapshotRoundTrip();
    void citationsRoundTrip();

private:
    static QString dbPath(const QTemporaryDir &tmp)
    {
        return QDir(tmp.path()).filePath(QStringLiteral("metadata.db"));
    }

    /// 建立可用的 store：返回前保证 schema 已就绪
    static bool prepare(Database &db, QTemporaryDir &tmp, QString *error)
    {
        if (!db.open(dbPath(tmp), error))
            return false;
        return db.ensureSchema(error);
    }

    static ChunkRecord makeChunk(int ordinal, const QString &text, const QString &label, int page)
    {
        ChunkRecord chunk;
        chunk.ordinal = ordinal;
        chunk.text = text;
        chunk.locKind = QStringLiteral("page");
        chunk.locLabel = label;
        chunk.locPage = page;
        chunk.charCount = text.size();
        chunk.tokenEst = text.size() / 2;
        return chunk;
    }
};

void TstLibraryStore::unavailableWithoutDatabase()
{
    LibraryStore store(nullptr);
    QVERIFY(!store.isAvailable());
    QVERIFY(!store.ftsAvailable());

    QString error;
    QVERIFY(store.collections(&error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(store.allFiles(&error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    qint64 id = -1;
    QVERIFY(!store.insertCollection(QStringLiteral("x"), QString(), &id, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(id, qint64(0));
}

void TstLibraryStore::collectionCrud()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);
    QVERIFY(store.isAvailable());

    qint64 beta = 0;
    qint64 alpha = 0;
    QVERIFY2(store.insertCollection(QStringLiteral("beta"), QStringLiteral("工程资料"), &beta, &error),
             qPrintable(error));
    QVERIFY(beta > 0);
    QVERIFY(store.insertCollection(QStringLiteral("Alpha"), QString(), &alpha, &error));
    QVERIFY(alpha > 0);

    // 列表按名称排序（大小写不敏感）：Alpha 在 beta 前
    const QList<CollectionRecord> list = store.collections(&error);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).id, alpha);
    QCOMPARE(list.at(1).id, beta);
    QCOMPARE(list.at(1).description, QStringLiteral("工程资料"));
    QVERIFY(list.at(0).createdAt > 0);

    CollectionRecord found;
    QVERIFY(store.collectionByName(QStringLiteral("beta"), &found, &error));
    QCOMPARE(found.id, beta);
    QVERIFY(!store.collectionByName(QStringLiteral("gamma"), &found, &error));

    QVERIFY2(store.updateCollection(beta, QStringLiteral("Beta 重命名"),
                                    QStringLiteral("施工规则"), &error),
             qPrintable(error));
    QVERIFY(store.collectionByName(QStringLiteral("Beta 重命名"), &found, &error));
    QCOMPARE(found.description, QStringLiteral("施工规则"));

    QVERIFY(store.removeCollection(alpha, &error));
    QCOMPARE(store.collections(&error).size(), 1);

    // 名称空串必须被拒绝
    error.clear();
    QVERIFY(!store.insertCollection(QString(), QString(), nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TstLibraryStore::duplicateCollectionNameRejected()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);
    QVERIFY(store.insertCollection(QStringLiteral("物业工程"), QString(), nullptr, &error));

    error.clear();
    QVERIFY(!store.insertCollection(QStringLiteral("物业工程"), QString(), nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("已存在")));
}

void TstLibraryStore::removingCollectionKeepsFilesAndOtherMembership()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    qint64 first = 0;
    qint64 second = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库一"), QString(), &first, &error));
    QVERIFY(store.insertCollection(QStringLiteral("库二"), QString(), &second, &error));

    LibraryFileRecord file;
    file.displayName = QStringLiteral("施工规则.pdf");
    file.sourceKind = QStringLiteral("folder");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("施工规则.pdf"));
    file.kind = QStringLiteral("text");
    file.ext = QStringLiteral("pdf");
    qint64 fileId = 0;
    QVERIFY2(store.insertFile(file, &fileId, &error), qPrintable(error));

    QVERIFY(store.linkFileToCollection(fileId, first, &error));
    QVERIFY(store.linkFileToCollection(fileId, second, &error));

    // 删除「库一」：文件记录与「库二」的归属都必须保留（安全红线）
    QVERIFY(store.removeCollection(first, &error));
    QCOMPARE(store.collections(&error).size(), 1);
    QCOMPARE(store.filesInCollection(second, &error).size(), 1);

    LibraryFileRecord kept;
    QVERIFY(store.fileById(fileId, &kept, &error));
    QCOMPARE(kept.displayName, QStringLiteral("施工规则.pdf"));
    QCOMPARE(store.collectionIdsOfFile(fileId, &error), QList<qint64>{ second });
}

void TstLibraryStore::fileMembershipIsManyToManyAndIdempotent()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);
    qint64 c1 = 0;
    qint64 c2 = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库一"), QString(), &c1, &error));
    QVERIFY(store.insertCollection(QStringLiteral("库二"), QString(), &c2, &error));

    LibraryFileRecord file;
    file.displayName = QStringLiteral("预算表.xlsx");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("budget.xlsx"));
    file.kind = QStringLiteral("text");
    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));

    QVERIFY(store.linkFileToCollection(fileId, c1, &error));
    QVERIFY(store.linkFileToCollection(fileId, c2, &error));
    // 重复关联幂等：不报错、不产生重复行
    QVERIFY(store.linkFileToCollection(fileId, c1, &error));
    QCOMPARE(store.collectionIdsOfFile(fileId, &error).size(), 2);

    // 解除关联只删关系，文件记录保留
    QVERIFY(store.unlinkFileFromCollection(fileId, c1, &error));
    QCOMPARE(store.collectionIdsOfFile(fileId, &error), QList<qint64>{ c2 });
    QVERIFY(store.fileById(fileId, nullptr, &error));

    error.clear();
    QVERIFY(!store.insertFile(LibraryFileRecord(), nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TstLibraryStore::duplicateSourcePathRejected()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);
    LibraryFileRecord file;
    file.displayName = QStringLiteral("a.txt");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("a.txt"));
    file.kind = QStringLiteral("text");

    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));

    error.clear();
    qint64 again = 0;
    QVERIFY(!store.insertFile(file, &again, &error));
    QVERIFY(error.contains(QStringLiteral("已在资料库")));
    QCOMPARE(again, qint64(0));
}

void TstLibraryStore::fileRoundTripAndUpdate()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    LibraryFileRecord file;
    file.displayName = QStringLiteral("上传.pdf");
    file.sourceKind = QStringLiteral("upload");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("library/上传.pdf"));
    file.originPath = QStringLiteral("D:/下载/上传.pdf");
    file.ext = QStringLiteral("pdf");
    file.kind = QStringLiteral("text");
    file.sizeBytes = 2048;
    file.mtime = 1699999999000;
    file.contentHash = QStringLiteral("hash-1");

    qint64 fileId = 0;
    QVERIFY2(store.insertFile(file, &fileId, &error), qPrintable(error));

    LibraryFileRecord loaded;
    QVERIFY(store.fileById(fileId, &loaded, &error));
    QCOMPARE(loaded.originPath, QStringLiteral("D:/下载/上传.pdf"));
    QCOMPARE(loaded.sizeBytes, qint64(2048));
    QCOMPARE(loaded.status, QStringLiteral("pending"));
    QCOMPARE(loaded.folderLinkId, qint64(0));  // NULL 读回 0

    // 入库后状态推进：索引完成
    loaded.status = QStringLiteral("indexed");
    loaded.chunkCount = 3;
    loaded.indexedAt = 1700000000000;
    QVERIFY2(store.updateFile(loaded, &error), qPrintable(error));

    LibraryFileRecord updated;
    QVERIFY(store.fileById(fileId, &updated, &error));
    QCOMPARE(updated.status, QStringLiteral("indexed"));
    QCOMPARE(updated.chunkCount, 3);
    QCOMPARE(updated.indexedAt, qint64(1700000000000));
    QCOMPARE(store.allFiles(&error).size(), 1);

    error.clear();
    QVERIFY(!store.updateFile(LibraryFileRecord(), &error));  // id 无效
    QVERIFY(!error.isEmpty());
}

void TstLibraryStore::replaceChunksIsAtomicAndIdempotent()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    LibraryFileRecord file;
    file.displayName = QStringLiteral("手册.pdf");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("手册.pdf"));
    file.kind = QStringLiteral("text");
    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));

    QList<ChunkRecord> chunks;
    chunks << makeChunk(0, QStringLiteral("第一章 总则"), QStringLiteral("第 1 页"), 1)
           << makeChunk(1, QStringLiteral("项目A | 预算表 | 1200万"), QStringLiteral("第 3 页"), 3)
           << makeChunk(2, QStringLiteral("第二章 施工"), QStringLiteral("第 5 页"), 5);

    QVERIFY2(store.replaceChunks(fileId, chunks, &error), qPrintable(error));

    const QList<ChunkRecord> stored = store.chunksOfFile(fileId, &error);
    QCOMPARE(stored.size(), 3);
    QCOMPARE(stored.at(0).ordinal, 0);
    QCOMPARE(stored.at(1).text, QStringLiteral("项目A | 预算表 | 1200万"));
    QCOMPARE(stored.at(1).locLabel, QStringLiteral("第 3 页"));
    QCOMPARE(stored.at(1).locPage, 3);
    QCOMPARE(stored.at(1).locKind, QStringLiteral("page"));
    QCOMPARE(stored.at(2).ordinal, 2);
    QVERIFY(stored.at(0).id > 0);

    // chunk_count 由存储层按实际写入条数回填
    LibraryFileRecord after;
    QVERIFY(store.fileById(fileId, &after, &error));
    QCOMPARE(after.chunkCount, 3);

    // 重新切块：旧片段必须被整体替换（不残留、不重复）
    QList<ChunkRecord> redo;
    redo << makeChunk(0, QStringLiteral("改版后唯一片段"), QStringLiteral("第 1 页"), 1);
    QVERIFY(store.replaceChunks(fileId, redo, &error));
    QCOMPARE(store.chunksOfFile(fileId, &error).size(), 1);
    QVERIFY(store.fileById(fileId, &after, &error));
    QCOMPARE(after.chunkCount, 1);

    // 清空片段也要允许（例如文件变成 no_text_layer）
    QVERIFY(store.replaceChunks(fileId, {}, &error));
    QVERIFY(store.chunksOfFile(fileId, &error).isEmpty());

    error.clear();
    QVERIFY(!store.replaceChunks(0, {}, &error));
    QVERIFY(!error.isEmpty());
}

void TstLibraryStore::neighborRangeAndChunkById()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    LibraryFileRecord file;
    file.displayName = QStringLiteral("规则.md");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("规则.md"));
    file.kind = QStringLiteral("text");
    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));

    QList<ChunkRecord> chunks;
    for (int i = 0; i < 5; ++i) {
        chunks << makeChunk(i, QStringLiteral("段落 %1").arg(i),
                            QStringLiteral("第 %1 段").arg(i + 1), 1);
    }
    QVERIFY(store.replaceChunks(fileId, chunks, &error));

    const QList<ChunkRecord> all = store.chunksOfFile(fileId, &error);
    QCOMPARE(all.size(), 5);

    // 邻居扩展：命中 ordinal=2，半径 1 → [1,3]
    const QList<ChunkRecord> neighbors = store.chunksInOrdinalRange(fileId, 1, 3, &error);
    QCOMPARE(neighbors.size(), 3);
    QCOMPARE(neighbors.at(0).ordinal, 1);
    QCOMPARE(neighbors.at(2).ordinal, 3);

    // 边界：起点为 0 时不能越界到负数区间
    QCOMPARE(store.chunksInOrdinalRange(fileId, 0, 0, &error).size(), 1);
    QVERIFY(store.chunksInOrdinalRange(fileId, 9, 12, &error).isEmpty());

    ChunkRecord one;
    QVERIFY(store.chunkById(all.at(2).id, &one, &error));
    QCOMPARE(one.text, QStringLiteral("段落 2"));
    QVERIFY(!store.chunkById(99999, &one, &error));

    const QList<ChunkRecord> picked = store.chunksByIds({ all.at(0).id, all.at(4).id }, &error);
    QCOMPARE(picked.size(), 2);
    QVERIFY(store.chunksByIds({}, &error).isEmpty());
}

void TstLibraryStore::keywordSearchRespectsCollectionFilterAndLimit()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    qint64 c1 = 0;
    qint64 c2 = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库一"), QString(), &c1, &error));
    QVERIFY(store.insertCollection(QStringLiteral("库二"), QString(), &c2, &error));

    LibraryFileRecord fileA;
    fileA.displayName = QStringLiteral("A.pdf");
    fileA.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("A.pdf"));
    fileA.kind = QStringLiteral("text");
    qint64 idA = 0;
    QVERIFY(store.insertFile(fileA, &idA, &error));
    QVERIFY(store.linkFileToCollection(idA, c1, &error));

    LibraryFileRecord fileB;
    fileB.displayName = QStringLiteral("B.pdf");
    fileB.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("B.pdf"));
    fileB.kind = QStringLiteral("text");
    qint64 idB = 0;
    QVERIFY(store.insertFile(fileB, &idB, &error));
    QVERIFY(store.linkFileToCollection(idB, c2, &error));

    QVERIFY(store.replaceChunks(idA,
                                { makeChunk(0, QStringLiteral("项目A | 预算表 | 1200万"),
                                            QStringLiteral("第 3 页"), 3) },
                                &error));
    QVERIFY(store.replaceChunks(idB,
                                { makeChunk(0, QStringLiteral("项目B | 预算表 | 800万"),
                                            QStringLiteral("第 2 页"), 2) },
                                &error));

    // 不限库：两个文件的片段都能命中
    const QList<qint64> all = store.searchChunkIds(QStringLiteral("预算表"), {}, 0, &error);
    QCOMPARE(all.size(), 2);

    // 限定「库一」：只命中 A 的片段
    const QList<qint64> inFirst = store.searchChunkIds(QStringLiteral("预算表"), { c1 }, 0, &error);
    QCOMPARE(inFirst.size(), 1);
    ChunkRecord hit;
    QVERIFY(store.chunkById(inFirst.at(0), &hit, &error));
    QCOMPARE(hit.fileId, idA);
    QCOMPARE(hit.locLabel, QStringLiteral("第 3 页"));

    // limit 生效
    QCOMPARE(store.searchChunkIds(QStringLiteral("预算表"), {}, 1, &error).size(), 1);

    // 短关键词（trigram 不可用的长度）走 LIKE 降级，同样必须命中
    QCOMPARE(store.searchChunkIds(QStringLiteral("预算"), {}, 0, &error).size(), 2);

    // LIKE 通配符必须被转义：字面 % 不应匹配任意内容
    QVERIFY(store.searchChunkIds(QStringLiteral("%"), {}, 0, &error).isEmpty());

    // 空关键词给出明确错误而不是崩溃
    error.clear();
    QVERIFY(store.searchChunkIds(QStringLiteral("   "), {}, 0, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 库里没有的内容返回空，且不报错
    error.clear();
    QVERIFY(store.searchChunkIds(QStringLiteral("完全不存在的关键词"), {}, 0, &error).isEmpty());
    QVERIFY(error.isEmpty());
}

void TstLibraryStore::removingFileRecordNeverTouchesDiskFile()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    // 磁盘上放一个真实文件，模拟用户原文件 / 关联文件夹里的文件
    const QString realPath = QDir(tmp.path()).filePath(QStringLiteral("业主原文件.txt"));
    QFile real(realPath);
    QVERIFY(real.open(QIODevice::WriteOnly));
    real.write("原始内容");
    real.close();

    LibraryFileRecord file;
    file.displayName = QStringLiteral("业主原文件.txt");
    file.sourceKind = QStringLiteral("folder");
    file.sourcePath = realPath;  // 关联文件夹：source_path 就是用户源文件路径
    file.kind = QStringLiteral("text");
    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));

    qint64 c1 = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库一"), QString(), &c1, &error));
    QVERIFY(store.linkFileToCollection(fileId, c1, &error));
    QVERIFY(store.replaceChunks(fileId, { makeChunk(0, QStringLiteral("预算表"), QStringLiteral("第 1 行"), 1) },
                                &error));
    QVERIFY(!store.searchChunkIds(QStringLiteral("预算表"), {}, 0, &error).isEmpty());

    QVERIFY2(store.removeFile(fileId, &error), qPrintable(error));

    // 索引被清理：文件记录、片段、关键词索引都不再能查到
    QVERIFY(!store.fileById(fileId, nullptr, &error));
    QVERIFY(store.chunksOfFile(fileId, &error).isEmpty());
    QVERIFY(store.searchChunkIds(QStringLiteral("预算表"), {}, 0, &error).isEmpty());

    // 红线：磁盘上的真实文件必须原样保留
    QVERIFY(QFile::exists(realPath));
    QFile check(realPath);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("原始内容"));
}

void TstLibraryStore::folderLinkSnapshotRoundTrip()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    qint64 collectionId = 0;
    QVERIFY(store.insertCollection(QStringLiteral("工程资料"), QString(), &collectionId, &error));

    FolderLinkRecord link;
    link.collectionId = collectionId;
    link.rootPath = QDir(tmp.path()).filePath(QStringLiteral("工程"));
    qint64 linkId = 0;
    QVERIFY2(store.insertFolderLink(link, &linkId, &error), qPrintable(error));
    QVERIFY(linkId > 0);

    QList<FolderLinkRecord> links = store.folderLinks(collectionId, &error);
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.at(0).status, QStringLiteral("empty"));
    QCOMPARE(links.at(0).snapshotAt, qint64(0));

    // 手动重建：写入快照时间与文件数
    QVERIFY2(store.updateFolderLinkSnapshot(linkId, 1700000000000, 12, QStringLiteral("ready"),
                                            QString(), &error),
             qPrintable(error));
    links = store.folderLinks(collectionId, &error);
    QCOMPARE(links.at(0).fileCount, 12);
    QCOMPARE(links.at(0).status, QStringLiteral("ready"));
    QCOMPARE(links.at(0).snapshotAt, qint64(1700000000000));
    QCOMPARE(store.folderLinks(0, &error).size(), 1);  // 不限库

    // 入库文件挂在该关联下
    LibraryFileRecord file;
    file.displayName = QStringLiteral("清单.xlsx");
    file.sourceKind = QStringLiteral("folder");
    file.sourcePath = QDir(link.rootPath).filePath(QStringLiteral("清单.xlsx"));
    file.folderLinkId = linkId;
    file.kind = QStringLiteral("text");
    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));

    LibraryFileRecord stored;
    QVERIFY(store.fileById(fileId, &stored, &error));
    QCOMPARE(stored.folderLinkId, linkId);

    // 解除关联文件夹：只删关联记录，文件索引记录保留（是否连带清理由 core 层决定）
    QVERIFY(store.removeFolderLink(linkId, &error));
    QVERIFY(store.folderLinks(collectionId, &error).isEmpty());
    QVERIFY(store.fileById(fileId, nullptr, &error));

    error.clear();
    QVERIFY(!store.insertFolderLink(FolderLinkRecord(), nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TstLibraryStore::citationsRoundTrip()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY2(prepare(db, tmp, &error), qPrintable(error));

    LibraryStore store(&db);

    LibraryFileRecord file;
    file.displayName = QStringLiteral("手册.pdf");
    file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("手册.pdf"));
    file.kind = QStringLiteral("text");
    qint64 fileId = 0;
    QVERIFY(store.insertFile(file, &fileId, &error));
    QVERIFY(store.replaceChunks(fileId, { makeChunk(0, QStringLiteral("预算 1200万"), QStringLiteral("第 3 页"), 3) },
                                &error));
    const qint64 chunkId = store.chunksOfFile(fileId, &error).at(0).id;

    CitationRecord citation;
    citation.messageId = 42;
    citation.conversationId = 7;
    citation.chunkId = chunkId;
    citation.fileId = fileId;
    citation.collectionId = 3;
    citation.locLabel = QStringLiteral("第 3 页");
    citation.rank = 0;
    citation.score = 0.87;
    qint64 citationId = 0;
    QVERIFY2(store.appendCitation(citation, &citationId, &error), qPrintable(error));
    QVERIFY(citationId > 0);

    citation.rank = 1;
    citation.score = 0.5;
    citation.locLabel = QStringLiteral("第 5 页");
    QVERIFY(store.appendCitation(citation, nullptr, &error));

    QList<CitationRecord> list = store.citationsOfMessage(42, &error);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).rank, 0);  // 按 rank 升序
    QCOMPARE(list.at(1).rank, 1);
    QVERIFY(list.at(0).score > 0.8);

    // citations 无外键：删除消息 / 会话时必须显式清理
    QVERIFY(store.removeCitationsOfMessage(42, &error));
    QVERIFY(store.citationsOfMessage(42, &error).isEmpty());

    QVERIFY(store.appendCitation(citation, nullptr, &error));
    QVERIFY(store.removeCitationsOfConversation(7, &error));
    QVERIFY(store.citationsOfMessage(42, &error).isEmpty());
}

QTEST_GUILESS_MAIN(TstLibraryStore)
#include "tst_librarystore.moc"
