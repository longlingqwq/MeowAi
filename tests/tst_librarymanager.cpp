#include "core/library/CollectionManager.h"
#include "core/library/LibraryManager.h"
#include "storage/Database.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

/**
 * 资料库文件与小库管理测试（core 层 M2）：
 *  - 上传导入：复制进库内目录，**原文件不动**；
 *  - 关联文件夹：快照登记（含 video 只登记不索引、未知扩展名不入库）、手动重建的
 *    新增 / 变更 / 缺失识别；
 *  - **安全红线（DoD #7）**：删除小库、解除关联、移除文件记录都不会删除用户磁盘上的文件；
 *    只有我们自己复制进库内的副本会被清理。
 */
class TstLibraryManager : public QObject
{
    Q_OBJECT

private slots:
    void unavailableManagersFailGracefully();
    void importCopiesIntoLibraryAndKeepsOriginal();
    void importRejectsMissingUnknownAndManagedCopy();
    void importFilesPartialFailureReportsAndKeepsSuccess();
    void folderSnapshotRegistersKindsAndSkipsUnknown();
    void folderRebuildDetectsAddedChangedMissing();
    void linkFolderRejectsDuplicateAndMissingRoot();
    void unlinkFolderKeepsUserFilesAndDetachesSharedRecords();
    void removeFileRecordRemovesUploadCopyOnly();
    void removeCollectionKeepsUserFilesOnDisk();

private:
    struct Fixture
    {
        QTemporaryDir tmp;
        AppPaths paths;
        Database db;
        std::unique_ptr<LibraryStore> store;
        std::unique_ptr<LibraryManager> files;
        std::unique_ptr<CollectionManager> collections;

        Fixture()
            : paths(QDir(tmp.path()).filePath(QStringLiteral("appdata")))
        {
        }

        bool init(QString *error)
        {
            if (!paths.ensureLayout(error))
                return false;
            if (!db.open(QDir(paths.root()).filePath(QStringLiteral("metadata.db")), error))
                return false;
            if (!db.ensureSchema(error))
                return false;
            store = std::make_unique<LibraryStore>(&db);
            files = std::make_unique<LibraryManager>(paths, store.get());
            collections = std::make_unique<CollectionManager>(store.get(), files.get());
            return true;
        }

        /// 在测试沙箱里造一个用户文件（库外），返回绝对路径
        QString userFile(const QString &relative, const QByteArray &content) const
        {
            const QString path = QDir(tmp.path()).filePath(QStringLiteral("sources/") + relative);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(content);
            file.close();
            return path;
        }

        QString userDir(const QString &relative) const
        {
            const QString path = QDir(tmp.path()).filePath(QStringLiteral("sources/") + relative);
            QDir().mkpath(path);
            return path;
        }

        qint64 newCollection(const QString &name) const
        {
            qint64 id = 0;
            QString error;
            collections->createCollection(name, QString(), &id, &error);
            return id;
        }
    };

    static bool fileExistsWithContent(const QString &path, const QByteArray &content)
    {
        if (!QFile::exists(path))
            return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QByteArray data = file.readAll();
        file.close();
        return data == content;
    }
};

void TstLibraryManager::unavailableManagersFailGracefully()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    LibraryManager files(AppPaths(QDir(tmp.path()).filePath(QStringLiteral("a"))), nullptr);
    QVERIFY(!files.isAvailable());

    QString error;
    qint64 id = 0;
    QVERIFY(!files.importFile(QStringLiteral("x.txt"), &id, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(id, qint64(0));

    error.clear();
    QVERIFY(!files.importFiles({ QStringLiteral("x.txt") }, nullptr, &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    FolderSnapshotResult stats;
    QVERIFY(!files.linkFolder(1, QStringLiteral("x"), nullptr, &stats, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!files.rebuildFolder(1, &stats, &error));
    QVERIFY(!files.unlinkFolder(1, &error));
    QVERIFY(!files.removeFileRecord(1, &error));
    QVERIFY(files.allFiles(&error).isEmpty());
    QVERIFY(files.folderLinks(0, &error).isEmpty());

    CollectionManager collections(nullptr, nullptr);
    QVERIFY(!collections.isAvailable());
    error.clear();
    QVERIFY(!collections.createCollection(QStringLiteral("x"), QString(), nullptr, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!collections.removeCollection(1, &error));
    QVERIFY(collections.collections(&error).isEmpty());
}

void TstLibraryManager::importCopiesIntoLibraryAndKeepsOriginal()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const QByteArray content("预算表内容 1200万");
    const QString source = fixture.userFile(QStringLiteral("预算表.txt"), content);

    qint64 fileId = 0;
    QVERIFY2(fixture.files->importFile(source, &fileId, &error), qPrintable(error));
    QVERIFY(fileId > 0);

    LibraryFileRecord record;
    QVERIFY(fixture.store->fileById(fileId, &record, &error));
    QCOMPARE(record.sourceKind, QStringLiteral("upload"));
    QCOMPARE(record.displayName, QStringLiteral("预算表.txt"));
    QCOMPARE(record.kind, QStringLiteral("text"));
    QCOMPARE(record.status, QStringLiteral("pending"));
    QCOMPARE(record.originPath, QFileInfo(source).absoluteFilePath());

    // 副本落在库内目录，内容与原文件一致
    QVERIFY(fixture.files->isManagedCopy(record.sourcePath));
    QVERIFY(fileExistsWithContent(record.sourcePath, content));
    // 原文件毫发无损
    QVERIFY(fileExistsWithContent(source, content));

    // 重名文件再导入 → 自动改名，不覆盖既有副本
    qint64 secondId = 0;
    QVERIFY2(fixture.files->importFile(source, &secondId, &error), qPrintable(error));
    LibraryFileRecord second;
    QVERIFY(fixture.store->fileById(secondId, &second, &error));
    QVERIFY(second.sourcePath != record.sourcePath);
    QCOMPARE(second.displayName, QStringLiteral("预算表.txt"));
    QVERIFY(fileExistsWithContent(second.sourcePath, content));

    // 明文不入日志：这里顺带确认 contentHash 只落指纹、不含内容
    QVERIFY(record.contentHash.startsWith(QStringLiteral("sha256:")));
    QVERIFY(!record.contentHash.contains(QStringLiteral("1200")));
}

void TstLibraryManager::importRejectsMissingUnknownAndManagedCopy()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    qint64 fileId = 0;
    // 不存在的来源
    QVERIFY(!fixture.files->importFile(QStringLiteral("D:/不存在/文件.txt"), &fileId, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(fileId, qint64(0));

    // 不支持的类型（本次范围外）
    error.clear();
    const QString archive = fixture.userFile(QStringLiteral("归档.zip"), "PK");
    QVERIFY(!fixture.files->importFile(archive, &fileId, &error));
    QVERIFY(error.contains(QStringLiteral("暂不支持")));
    QVERIFY(fileExistsWithContent(archive, "PK"));

    // 已在库内目录的文件不得重复导入（防止自我复制 / 破坏副本）
    error.clear();
    const QString source = fixture.userFile(QStringLiteral("在库内.txt"), "abc");
    QVERIFY(fixture.files->importFile(source, &fileId, &error));
    LibraryFileRecord record;
    QVERIFY(fixture.store->fileById(fileId, &record, &error));
    error.clear();
    QVERIFY(!fixture.files->importFile(record.sourcePath, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("资料库目录")));

    // 空列表批量导入：给明确错误而不是静默成功
    error.clear();
    QVERIFY(!fixture.files->importFiles({}, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(fixture.store->allFiles(&error).size(), 1);
}

void TstLibraryManager::importFilesPartialFailureReportsAndKeepsSuccess()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const QString good = fixture.userFile(QStringLiteral("好文件.txt"), "ok");
    const QString bad = QStringLiteral("D:/不存在/坏文件.txt");

    QList<qint64> ids;
    QVERIFY(!fixture.files->importFiles({ good, bad }, &ids, &error));
    QVERIFY(error.contains(QStringLiteral("好文件.txt")) || error.contains(QStringLiteral("坏文件.txt")));
    QCOMPARE(ids.size(), 1);  // 成功的保留，可继续归库
    QCOMPARE(fixture.store->allFiles(&error).size(), 1);

    // 全部成功 → 返回 true 且清空错误
    error = QStringLiteral("残留");
    QList<qint64> all;
    const QString another = fixture.userFile(QStringLiteral("另一个.md"), "# 标题");
    QVERIFY(fixture.files->importFiles({ another }, &all, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(all.size(), 1);
}

void TstLibraryManager::folderSnapshotRegistersKindsAndSkipsUnknown()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const qint64 collectionId = fixture.newCollection(QStringLiteral("物业工程"));
    QVERIFY(collectionId > 0);

    const QString folder = fixture.userDir(QStringLiteral("工程"));
    fixture.userFile(QStringLiteral("工程/施工规则.txt"), "第一章 总则");
    fixture.userFile(QStringLiteral("工程/现场照片.png"), "PNGDATA");
    fixture.userFile(QStringLiteral("工程/培训视频.mp4"), "MP4DATA");
    fixture.userFile(QStringLiteral("工程/安装包.bin"), "BIN");
    fixture.userFile(QStringLiteral("工程/子目录/验收.md"), "# 验收");

    qint64 linkId = 0;
    FolderSnapshotResult stats;
    QVERIFY2(fixture.files->linkFolder(collectionId, folder, &linkId, &stats, &error),
             qPrintable(error));
    QVERIFY(linkId > 0);
    QCOMPARE(stats.added, 4);        // txt + png + mp4 + md
    QCOMPARE(stats.unsupported, 1);  // 视频：登记但不索引
    QCOMPARE(stats.skipped, 1);      // .bin：完全不入库

    // 文件已归入小库，且带类别与状态
    const QList<LibraryFileRecord> inCollection = fixture.files->filesInCollection(collectionId, &error);
    QCOMPARE(inCollection.size(), 4);
    for (const LibraryFileRecord &record : inCollection) {
        QCOMPARE(record.sourceKind, QStringLiteral("folder"));
        QVERIFY(record.folderLinkId == linkId);
        QVERIFY(!fixture.files->isManagedCopy(record.sourcePath));  // 用户文件：不在库内
        if (record.kind == QStringLiteral("video")) {
            QCOMPARE(record.status, QStringLiteral("unsupported"));
            QVERIFY(!record.errorText.isEmpty());
        } else {
            QCOMPARE(record.status, QStringLiteral("pending"));
        }
    }

    const QList<FolderLinkRecord> links = fixture.files->folderLinks(collectionId, &error);
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.at(0).fileCount, 4);
    QCOMPARE(links.at(0).status, QStringLiteral("ready"));
    QVERIFY(links.at(0).snapshotAt > 0);
}

void TstLibraryManager::folderRebuildDetectsAddedChangedMissing()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const qint64 collectionId = fixture.newCollection(QStringLiteral("工程"));
    const QString folder = fixture.userDir(QStringLiteral("工程"));
    fixture.userFile(QStringLiteral("工程/保持.txt"), "unchanged-content");
    fixture.userFile(QStringLiteral("工程/变动.txt"), "短");
    const QString removedPath = fixture.userFile(QStringLiteral("工程/待删.md"), "# 待删");

    qint64 linkId = 0;
    FolderSnapshotResult stats;
    QVERIFY(fixture.files->linkFolder(collectionId, folder, &linkId, &stats, &error));
    QCOMPARE(stats.added, 3);

    {   // 改动：长度变化 → 视为内容变更
        QFile file(fixture.userFile(QStringLiteral("工程/变动.txt"), "已经变成更长的一段内容"));
        file.close();
    }
    QVERIFY(QFile::remove(removedPath));               // 用户自己删了文件（不是我们删的）
    fixture.userFile(QStringLiteral("工程/新增.txt"), "added");

    FolderSnapshotResult rebuilt;
    QVERIFY2(fixture.files->rebuildFolder(linkId, &rebuilt, &error), qPrintable(error));
    QCOMPARE(rebuilt.added, 1);
    QCOMPARE(rebuilt.missing, 1);
    QCOMPARE(rebuilt.markedStale, 2);   // 变更 1 条 + 缺失 1 条
    QCOMPARE(rebuilt.unchanged, 1);

    QHash<QString, LibraryFileRecord> byName;
    for (const LibraryFileRecord &record : fixture.store->filesByFolderLink(linkId, &error))
        byName.insert(record.displayName, record);

    QCOMPARE(byName.value(QStringLiteral("保持.txt")).status, QStringLiteral("pending"));
    QCOMPARE(byName.value(QStringLiteral("变动.txt")).status, QStringLiteral("stale"));
    QCOMPARE(byName.value(QStringLiteral("待删.md")).status, QStringLiteral("stale"));
    QCOMPARE(byName.value(QStringLiteral("新增.txt")).status, QStringLiteral("pending"));

    const QList<FolderLinkRecord> links = fixture.files->folderLinks(collectionId, &error);
    QCOMPARE(links.at(0).fileCount, 3);  // 3 - 1 缺失 + 1 新增

    // 重建不是破坏性操作：磁盘上的用户文件不受影响
    QVERIFY(fileExistsWithContent(QDir(folder).filePath(QStringLiteral("保持.txt")),
                                  "unchanged-content"));
}

void TstLibraryManager::linkFolderRejectsDuplicateAndMissingRoot()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const QString folder = fixture.userDir(QStringLiteral("工程"));
    fixture.userFile(QStringLiteral("工程/清单.xlsx"), "xlsx");

    const qint64 first = fixture.newCollection(QStringLiteral("库一"));
    const qint64 second = fixture.newCollection(QStringLiteral("库二"));

    qint64 linkId = 0;
    FolderSnapshotResult stats;
    QVERIFY(fixture.files->linkFolder(first, folder, &linkId, &stats, &error));

    // 同一物理文件夹不允许重复关联（跨小库亦然），避免文件记录归属歧义
    error.clear();
    qint64 again = 0;
    QVERIFY(!fixture.files->linkFolder(second, folder, nullptr, &stats, &error));
    QVERIFY(error.contains(QStringLiteral("已关联")));
    QCOMPARE(again, qint64(0));

    // 不存在 / 不是目录
    error.clear();
    QVERIFY(!fixture.files->linkFolder(first, QStringLiteral("D:/不存在/目录"), nullptr, &stats, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    const QString file = fixture.userFile(QStringLiteral("单文件.txt"), "x");
    QVERIFY(!fixture.files->linkFolder(first, file, nullptr, &stats, &error));
    QVERIFY(!error.isEmpty());

    // 失败路径不留半成品关联记录
    QCOMPARE(fixture.store->folderLinks(0, &error).size(), 1);
}

void TstLibraryManager::unlinkFolderKeepsUserFilesAndDetachesSharedRecords()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const qint64 owner = fixture.newCollection(QStringLiteral("工程"));
    const qint64 other = fixture.newCollection(QStringLiteral("共用"));
    const QString folder = fixture.userDir(QStringLiteral("工程"));
    const QString shared = fixture.userFile(QStringLiteral("工程/共用.txt"), "shared");
    const QString onlyHere = fixture.userFile(QStringLiteral("工程/独有.txt"), "only");

    qint64 linkId = 0;
    FolderSnapshotResult stats;
    QVERIFY(fixture.files->linkFolder(owner, folder, &linkId, &stats, &error));
    QCOMPARE(stats.added, 2);

    // 把其中一个文件也加入另一个小库：解除关联后它必须作为「脱离快照」的记录保留
    const QList<LibraryFileRecord> files = fixture.files->filesInCollection(owner, &error);
    qint64 sharedId = 0;
    for (const LibraryFileRecord &record : files) {
        if (record.sourcePath == QFileInfo(shared).absoluteFilePath())
            sharedId = record.id;
    }
    QVERIFY(sharedId > 0);
    QVERIFY(fixture.collections->addFileToCollection(sharedId, other, &error));

    QVERIFY2(fixture.files->unlinkFolder(linkId, &error), qPrintable(error));
    QVERIFY(fixture.files->folderLinks(owner, &error).isEmpty());

    // 共用文件：记录保留、脱离快照、仍归属另一个库
    LibraryFileRecord kept;
    QVERIFY(fixture.store->fileById(sharedId, &kept, &error));
    QCOMPARE(kept.folderLinkId, qint64(0));
    QCOMPARE(fixture.collections->collectionsOfFile(sharedId, &error), QList<qint64>{ other });

    // 唯一归属的快照文件：只删索引记录，磁盘文件必须还在
    QCOMPARE(fixture.files->filesInCollection(owner, &error).size(), 0);
    QVERIFY(fileExistsWithContent(shared, "shared"));
    QVERIFY(fileExistsWithContent(onlyHere, "only"));
    QCOMPARE(fixture.files->allFiles(&error).size(), 1);
}

void TstLibraryManager::removeFileRecordRemovesUploadCopyOnly()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    // ① 上传导入：记录移除后，库内副本清理、原文件保留
    const QString source = fixture.userFile(QStringLiteral("原始.txt"), "origin");
    qint64 uploadId = 0;
    QVERIFY(fixture.files->importFile(source, &uploadId, &error));
    LibraryFileRecord upload;
    QVERIFY(fixture.store->fileById(uploadId, &upload, &error));
    QVERIFY(QFile::exists(upload.sourcePath));

    QVERIFY2(fixture.files->removeFileRecord(uploadId, &error), qPrintable(error));
    QVERIFY(!QFile::exists(upload.sourcePath));            // 我们自己的副本被清理
    QVERIFY(fileExistsWithContent(source, "origin"));      // 用户原文件不动
    QVERIFY(!fixture.store->fileById(uploadId, nullptr, &error));

    // ② 关联文件夹：记录移除后磁盘上的用户文件必须原样保留
    const qint64 collectionId = fixture.newCollection(QStringLiteral("工程"));
    const QString folder = fixture.userDir(QStringLiteral("工程"));
    const QString folderFile = fixture.userFile(QStringLiteral("工程/规则.pdf"), "PDF");
    qint64 linkId = 0;
    FolderSnapshotResult stats;
    QVERIFY(fixture.files->linkFolder(collectionId, folder, &linkId, &stats, &error));

    const QList<LibraryFileRecord> links = fixture.store->filesByFolderLink(linkId, &error);
    QCOMPARE(links.size(), 1);
    QVERIFY2(fixture.files->removeFileRecord(links.at(0).id, &error), qPrintable(error));
    QVERIFY(fileExistsWithContent(folderFile, "PDF"));
    QVERIFY(fixture.store->filesByFolderLink(linkId, &error).isEmpty());

    // ③ 记录不存在 → 明确错误，不崩
    error.clear();
    QVERIFY(!fixture.files->removeFileRecord(99999, &error));
    QVERIFY(!error.isEmpty());
}

void TstLibraryManager::removeCollectionKeepsUserFilesOnDisk()
{
    Fixture fixture;
    QString error;
    QVERIFY2(fixture.init(&error), qPrintable(error));

    const qint64 doomed = fixture.newCollection(QStringLiteral("将被删除"));
    const qint64 keeper = fixture.newCollection(QStringLiteral("保留的库"));

    // 关联文件夹（含一个同时加入另一库的共用文件）
    const QString folder = fixture.userDir(QStringLiteral("工程"));
    const QString sharedFile = fixture.userFile(QStringLiteral("工程/共用.txt"), "shared");
    const QString exclusiveFile = fixture.userFile(QStringLiteral("工程/独有.txt"), "exclusive");
    qint64 linkId = 0;
    FolderSnapshotResult stats;
    QVERIFY(fixture.files->linkFolder(doomed, folder, &linkId, &stats, &error));
    QCOMPARE(stats.added, 2);

    qint64 sharedId = 0;
    for (const LibraryFileRecord &record : fixture.files->filesInCollection(doomed, &error)) {
        if (record.sourcePath == QFileInfo(sharedFile).absoluteFilePath())
            sharedId = record.id;
    }
    QVERIFY(sharedId > 0);
    QVERIFY(fixture.collections->addFileToCollection(sharedId, keeper, &error));

    // 上传导入到将被删除的库
    const QString uploadSource = fixture.userFile(QStringLiteral("上传原件.txt"), "upload-origin");
    qint64 uploadId = 0;
    QVERIFY(fixture.files->importFile(uploadSource, &uploadId, &error));
    QVERIFY(fixture.collections->addFileToCollection(uploadId, doomed, &error));
    LibraryFileRecord uploadRecord;
    QVERIFY(fixture.store->fileById(uploadId, &uploadRecord, &error));

    // 删除小库
    QVERIFY2(fixture.collections->removeCollection(doomed, &error), qPrintable(error));
    QVERIFY(fixture.collections->collections(&error).size() == 1);
    QCOMPARE(fixture.collections->collections(&error).at(0).id, keeper);

    // 用户磁盘文件全部保留（红线）
    QVERIFY(fileExistsWithContent(sharedFile, "shared"));
    QVERIFY(fileExistsWithContent(exclusiveFile, "exclusive"));
    QVERIFY(fileExistsWithContent(uploadSource, "upload-origin"));

    // 共用文件：保留记录并脱离已删快照，仍归属「保留的库」
    LibraryFileRecord keptShared;
    QVERIFY(fixture.store->fileById(sharedId, &keptShared, &error));
    QCOMPARE(keptShared.folderLinkId, qint64(0));
    QCOMPARE(fixture.collections->collectionsOfFile(sharedId, &error), QList<qint64>{ keeper });

    // 独有文件与库内副本：索引记录与副本一并清理，用户原文件不受影响
    QVERIFY(fixture.store->filesByFolderLink(linkId, &error).isEmpty());
    QVERIFY(fixture.store->fileBySourcePath(QFileInfo(exclusiveFile).absoluteFilePath(), nullptr, &error)
            == false);
    QVERIFY(!QFile::exists(uploadRecord.sourcePath));
    QVERIFY(!fixture.store->fileById(uploadId, nullptr, &error));
}

QTEST_GUILESS_MAIN(TstLibraryManager)
#include "tst_librarymanager.moc"
