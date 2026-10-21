#include "core/library/LibraryManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <utility>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

constexpr qint64 kFullHashLimit = 8 * 1024 * 1024;  // 8MB 以内做完整 SHA-256

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString &left, const QString &right)
{
#if defined(Q_OS_WIN)
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

}  // namespace

LibraryManager::LibraryManager(AppPaths paths, LibraryStore *store)
    : m_store(store)
    , m_paths(std::move(paths))
{
}

bool LibraryManager::isAvailable() const
{
    return m_store && m_store->isAvailable();
}

AppPaths LibraryManager::paths() const
{
    return m_paths;
}

// ---------------------------------------------------------------- 上传导入

bool LibraryManager::importFile(const QString &sourcePath, qint64 *fileId, QString *error)
{
    if (fileId) {
        *fileId = 0;
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法导入文件"));
        return false;
    }

    const QFileInfo info(sourcePath);
    if (sourcePath.trimmed().isEmpty() || !info.exists() || !info.isFile()) {
        setError(error, QStringLiteral("来源文件不存在或不是普通文件：%1").arg(sourcePath));
        return false;
    }
    if (isManagedCopy(info.absoluteFilePath())) {
        setError(error, QStringLiteral("该文件已位于资料库目录内，无需重复导入"));
        return false;
    }

    const LibraryKind kind = libraryKindForPath(sourcePath);
    if (kind == LibraryKind::Unknown) {
        setError(error, QStringLiteral("暂不支持的文件类型：.%1").arg(libraryExtension(sourcePath)));
        return false;
    }

    if (!QDir().mkpath(m_paths.libraryDir())) {
        setError(error, QStringLiteral("无法创建资料库目录：%1").arg(m_paths.libraryDir()));
        return false;
    }

    // 复制（绝不移动、绝不覆盖既有文件）：同名则自动改名
    const QString target = uniqueTargetPath(info.fileName());
    if (!QFile::copy(info.absoluteFilePath(), target)) {
        setError(error, QStringLiteral("复制到资料库失败（磁盘空间或权限不足）：%1").arg(info.fileName()));
        return false;
    }
    if (QFileInfo(target).size() != info.size()) {
        QFile::remove(target);
        setError(error, QStringLiteral("复制校验失败，已清理未完成的副本：%1").arg(info.fileName()));
        return false;
    }

    LibraryFileRecord record;
    record.displayName = info.fileName();
    record.sourceKind = QStringLiteral("upload");
    record.sourcePath = target;
    record.originPath = info.absoluteFilePath();  // 仅记录，绝不删除
    record.ext = libraryExtension(sourcePath);
    record.kind = libraryKindName(kind);
    record.sizeBytes = info.size();
    record.mtime = info.lastModified().toMSecsSinceEpoch();
    record.contentHash = contentHash(target);
    // 视频本次只登记不索引（渲染条件：库里能看见它，但不会被解析）
    record.status = kind == LibraryKind::Video ? QStringLiteral("unsupported")
                                               : QStringLiteral("pending");
    record.errorText = kind == LibraryKind::Video ? QStringLiteral("视频暂不支持（本次范围外）")
                                                  : QString();

    if (!m_store->insertFile(record, fileId, error)) {
        QFile::remove(target);  // 登记失败则不留孤儿副本
        return false;
    }
    return true;
}

bool LibraryManager::importFiles(const QStringList &sourcePaths, QList<qint64> *fileIds,
                                 QString *error)
{
    if (fileIds) {
        fileIds->clear();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法导入文件"));
        return false;
    }
    if (sourcePaths.isEmpty()) {
        setError(error, QStringLiteral("没有选择要导入的文件"));
        return false;
    }

    QStringList failures;
    for (const QString &path : sourcePaths) {
        qint64 fileId = 0;
        QString reason;
        if (importFile(path, &fileId, &reason)) {
            if (fileIds) {
                fileIds->append(fileId);
            }
        } else {
            failures.append(QStringLiteral("%1（%2）").arg(QFileInfo(path).fileName(), reason));
        }
    }

    if (error) {
        error->clear();
    }
    if (failures.isEmpty()) {
        return true;
    }
    setError(error, QStringLiteral("部分文件导入失败：%1").arg(failures.join(QStringLiteral("；"))));
    return false;
}

// ------------------------------------------------------------- 关联文件夹

bool LibraryManager::linkFolder(qint64 collectionId, const QString &rootPath, qint64 *linkId,
                                FolderSnapshotResult *result, QString *error)
{
    if (linkId) {
        *linkId = 0;
    }
    if (result) {
        *result = FolderSnapshotResult();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法关联文件夹"));
        return false;
    }

    const QFileInfo info(rootPath);
    if (rootPath.trimmed().isEmpty() || !info.exists() || !info.isDir()) {
        setError(error, QStringLiteral("文件夹不存在或不是目录：%1").arg(rootPath));
        return false;
    }

    // 一个物理文件夹只允许存在一条关联记录：避免「一个文件记录只能挂一个快照」的归属歧义
    const QString clean = normalizedPath(rootPath);
    const QList<FolderLinkRecord> existingLinks = m_store->folderLinks(0, error);
    for (const FolderLinkRecord &link : existingLinks) {
        if (samePath(normalizedPath(link.rootPath), clean)) {
            setError(error, QStringLiteral("该文件夹已关联到小库（关联 id=%1），请勿重复关联；"
                                           "需要让文件也属于其它小库时，请用「加入小库」")
                                .arg(link.id));
            return false;
        }
    }

    // 先扫描再落库：文件夹不可读 / 文件过多时不留半成品关联
    QList<SnapshotEntry> entries;
    FolderSnapshotResult stats;
    if (!scanFolder(clean, &entries, &stats, error)) {
        return false;
    }

    FolderLinkRecord link;
    link.collectionId = collectionId;
    link.rootPath = clean;
    link.snapshotAt = 0;
    link.fileCount = 0;
    link.status = QStringLiteral("empty");
    qint64 id = 0;
    if (!m_store->insertFolderLink(link, &id, error)) {
        return false;
    }
    link.id = id;
    if (linkId) {
        *linkId = id;
    }

    if (!applySnapshot(link, entries, &stats, error)) {
        return false;
    }
    if (result) {
        *result = stats;
    }
    return true;
}

bool LibraryManager::rebuildFolder(qint64 linkId, FolderSnapshotResult *result, QString *error)
{
    if (result) {
        *result = FolderSnapshotResult();
    }
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法重建快照"));
        return false;
    }

    FolderLinkRecord link;
    if (!findFolderLink(linkId, &link, error)) {
        if (error && error->isEmpty()) {
            setError(error, QStringLiteral("关联文件夹不存在：%1").arg(linkId));
        }
        return false;
    }

    QList<SnapshotEntry> entries;
    FolderSnapshotResult stats;
    if (!scanFolder(link.rootPath, &entries, &stats, error)) {
        m_store->updateFolderLinkSnapshot(link.id, link.snapshotAt, link.fileCount,
                                          QStringLiteral("failed"), QStringLiteral("重建失败：%1")
                                              .arg(error ? *error : QString()),
                                          nullptr);
        return false;
    }

    if (!applySnapshot(link, entries, &stats, error)) {
        return false;
    }
    if (result) {
        *result = stats;
    }
    return true;
}

bool LibraryManager::unlinkFolder(qint64 linkId, QString *error)
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法解除关联文件夹"));
        return false;
    }

    FolderLinkRecord link;
    if (!findFolderLink(linkId, &link, error)) {
        if (error && error->isEmpty()) {
            setError(error, QStringLiteral("关联文件夹不存在：%1").arg(linkId));
        }
        return false;
    }

    const QList<LibraryFileRecord> files = m_store->filesByFolderLink(link.id, error);
    for (const LibraryFileRecord &file : files) {
        QList<qint64> owners = m_store->collectionIdsOfFile(file.id, error);
        owners.removeAll(link.collectionId);
        if (owners.isEmpty()) {
            // 失去唯一归属：只删索引记录。folder 类的用户文件由 removeFileRecord 保证不动
            if (!removeFileRecord(file.id, error)) {
                return false;
            }
        } else {
            // 仍归属其它小库：先撤出被解除的小库，再脱离即将被删除的快照，记录本身保留
            if (!m_store->unlinkFileFromCollection(file.id, link.collectionId, error)) {
                return false;
            }
            LibraryFileRecord detached = file;
            detached.folderLinkId = 0;
            if (!m_store->updateFile(detached, error)) {
                return false;
            }
        }
    }

    return m_store->removeFolderLink(link.id, error);
}

// ------------------------------------------------------------------ 记录

bool LibraryManager::removeFileRecord(qint64 fileId, QString *error)
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法移除文件记录"));
        return false;
    }

    LibraryFileRecord file;
    if (!m_store->fileById(fileId, &file, error)) {
        if (error && error->isEmpty()) {
            setError(error, QStringLiteral("文件记录不存在：%1").arg(fileId));
        }
        return false;
    }

    if (!m_store->removeFile(fileId, error)) {
        return false;
    }

    // 只有我们自己复制进库内的副本才允许删除；用户原文件与关联文件夹里的文件永不触碰
    if (file.sourceKind == QStringLiteral("upload") && isManagedCopy(file.sourcePath)
        && QFile::exists(file.sourcePath)) {
        if (!QFile::remove(file.sourcePath)) {
            qWarning("MeowAi: 库内副本清理失败（索引已移除，磁盘留存副本）：%s",
                     qPrintable(file.sourcePath));
        }
    }
    return true;
}

// -------------------------------------------------------------- 只读视图

QList<LibraryFileRecord> LibraryManager::allFiles(QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取文件列表"));
        return {};
    }
    return m_store->allFiles(error);
}

QList<LibraryFileRecord> LibraryManager::filesInCollection(qint64 collectionId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取库内文件"));
        return {};
    }
    return m_store->filesInCollection(collectionId, error);
}

QList<FolderLinkRecord> LibraryManager::folderLinks(qint64 collectionId, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取关联文件夹"));
        return {};
    }
    return m_store->folderLinks(collectionId, error);
}

bool LibraryManager::isManagedCopy(const QString &path) const
{
    if (path.trimmed().isEmpty()) {
        return false;
    }
    const QString directory = QDir::cleanPath(m_paths.libraryDir());
    const QString candidate = normalizedPath(path);
    if (candidate.size() <= directory.size()) {
        return false;
    }
    return samePath(candidate.left(directory.size()), directory)
           && candidate.at(directory.size()) == QLatin1Char('/');
}

QString LibraryManager::contentHash(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return QString();
    }

    if (info.size() > kFullHashLimit) {
        // 大文件退化为代理指纹：重建快照时仍能识别「改没改」，代价可控
        return QStringLiteral("sz-mtime:%1:%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("sz-mtime:%1:%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(hash.result().toHex()));
}

// ------------------------------------------------------------------ 内部

bool LibraryManager::scanFolder(const QString &rootPath, QList<SnapshotEntry> *entries,
                                FolderSnapshotResult *result, QString *error) const
{
    entries->clear();

    QDirIterator iterator(rootPath, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const LibraryKind kind = libraryKindForPath(path);
        if (kind == LibraryKind::Unknown) {
            if (result) {
                ++result->skipped;
            }
            continue;
        }
        if (entries->size() >= kMaxSnapshotFiles) {
            setError(error, QStringLiteral("该文件夹内文件过多（超过 %1 个），已中止关联；"
                                           "请改用更具体的子文件夹")
                                .arg(kMaxSnapshotFiles));
            return false;
        }
        const QFileInfo info(path);
        SnapshotEntry entry;
        entry.path = QDir::cleanPath(info.absoluteFilePath());
        entry.displayName = info.fileName();
        entry.size = info.size();
        entry.mtime = info.lastModified().toMSecsSinceEpoch();
        entry.kind = kind;
        entries->append(entry);
        if (result && kind == LibraryKind::Video) {
            ++result->unsupported;
        }
    }
    return true;
}

bool LibraryManager::applySnapshot(const FolderLinkRecord &link, const QList<SnapshotEntry> &entries,
                                   FolderSnapshotResult *result, QString *error) const
{
    // 统计结构由调用方预置（扫描阶段已累计 skipped / unsupported），这里继续往上加
    FolderSnapshotResult local;
    FolderSnapshotResult &stats = result ? *result : local;

    const QList<LibraryFileRecord> existing = m_store->filesByFolderLink(link.id, error);
    QHash<QString, LibraryFileRecord> byPath;
    for (const LibraryFileRecord &record : existing) {
        byPath.insert(normalizedPath(record.sourcePath), record);
    }

    QSet<QString> seen;
    int inserted = 0;
    for (const SnapshotEntry &entry : entries) {
        const QString key = entry.path;
        seen.insert(key);

        if (!byPath.contains(key)) {
            // 该路径已被别的记录占用（理论上只剩历史数据）：复用记录并补归库，不重复插入
            LibraryFileRecord occupied;
            if (m_store->fileBySourcePath(key, &occupied, nullptr)) {
                // 幂等补归库：失败不阻塞整体快照（该文件已在库中，只是库归属可能已存在）
                m_store->linkFileToCollection(occupied.id, link.collectionId, nullptr);
                ++stats.unchanged;
                continue;
            }

            LibraryFileRecord record;
            record.displayName = entry.displayName;
            record.sourceKind = QStringLiteral("folder");
            record.sourcePath = key;  // 用户自己的文件，绝不复制、绝不删除
            record.originPath = QString();
            record.folderLinkId = link.id;
            record.ext = libraryExtension(key);
            record.kind = libraryKindName(entry.kind);
            record.sizeBytes = entry.size;
            record.mtime = entry.mtime;
            record.contentHash = contentHash(key);
            record.status = entry.kind == LibraryKind::Video ? QStringLiteral("unsupported")
                                                             : QStringLiteral("pending");
            record.errorText = entry.kind == LibraryKind::Video
                                   ? QStringLiteral("视频暂不支持（本次范围外）")
                                   : QString();
            qint64 fileId = 0;
            if (!m_store->insertFile(record, &fileId, error)) {
                return false;
            }
            if (!m_store->linkFileToCollection(fileId, link.collectionId, error)) {
                return false;
            }
            ++stats.added;
            ++inserted;
            continue;
        }

        LibraryFileRecord record = byPath.value(key);
        m_store->linkFileToCollection(record.id, link.collectionId, nullptr);  // 幂等补归库
        if (record.sizeBytes == entry.size && record.mtime == entry.mtime) {
            ++stats.unchanged;  // 未变化：保留原索引状态（可能已 indexed）
            continue;
        }
        record.sizeBytes = entry.size;
        record.mtime = entry.mtime;
        record.contentHash = contentHash(key);
        if (record.status != QStringLiteral("unsupported")) {
            record.status = QStringLiteral("stale");
            record.errorText = QStringLiteral("源文件已变更，需重建索引");
        }
        if (!m_store->updateFile(record, error)) {
            return false;
        }
        ++stats.markedStale;
    }

    // 快照里已消失的文件：只标记，不删除（用户可能只是临时移走）
    for (const LibraryFileRecord &record : existing) {
        if (seen.contains(normalizedPath(record.sourcePath))) {
            continue;
        }
        LibraryFileRecord stale = record;
        if (stale.status != QStringLiteral("unsupported")) {
            stale.status = QStringLiteral("stale");
            stale.errorText = QStringLiteral("源文件已不在关联文件夹中");
        }
        if (!m_store->updateFile(stale, error)) {
            return false;
        }
        ++stats.missing;
        ++stats.markedStale;
    }

    const int total = existing.size() - stats.missing + inserted;
    if (!m_store->updateFolderLinkSnapshot(link.id, QDateTime::currentMSecsSinceEpoch(), total,
                                           QStringLiteral("ready"), QString(), error)) {
        return false;
    }
    return true;
}

bool LibraryManager::findFolderLink(qint64 linkId, FolderLinkRecord *out, QString *error) const
{
    if (out) {
        *out = FolderLinkRecord();
    }
    const QList<FolderLinkRecord> links = m_store->folderLinks(0, error);
    for (const FolderLinkRecord &link : links) {
        if (link.id == linkId) {
            if (out) {
                *out = link;
            }
            return true;
        }
    }
    return false;
}

QString LibraryManager::uniqueTargetPath(const QString &displayName) const
{
    const QFileInfo info(displayName);
    const QString base = info.completeBaseName().isEmpty() ? QStringLiteral("file") : info.completeBaseName();
    const QString suffix = info.suffix();
    const auto build = [&suffix](const QString &stem) {
        return suffix.isEmpty() ? stem : stem + QLatin1Char('.') + suffix;
    };

    QString candidate = QDir(m_paths.libraryDir()).filePath(displayName);
    int attempt = 0;
    while (attempt < 10000
           && (QFile::exists(candidate) || m_store->fileBySourcePath(candidate, nullptr, nullptr))) {
        ++attempt;
        candidate = QDir(m_paths.libraryDir()).filePath(build(QStringLiteral("%1-%2").arg(base).arg(attempt)));
    }
    return candidate;
}
