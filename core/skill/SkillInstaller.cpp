#include "core/skill/SkillInstaller.h"

#include "core/skill/Skill.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <utility>

#if defined(MEOWAI_HAVE_QZIPREADER)
#include <private/qzipreader_p.h>
#endif

namespace {

QString formatMb(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
}

QString normalizedEntryName(const QString &name)
{
    return QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

quint16 readLe16(const QByteArray &blob, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(blob.at(offset))
                                | (static_cast<quint16>(static_cast<quint8>(blob.at(offset + 1))) << 8));
}

quint32 readLe32(const QByteArray &blob, int offset)
{
    return static_cast<quint32>(static_cast<quint8>(blob.at(offset)))
           | (static_cast<quint32>(static_cast<quint8>(blob.at(offset + 1))) << 8)
           | (static_cast<quint32>(static_cast<quint8>(blob.at(offset + 2))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(blob.at(offset + 3))) << 24);
}

/**
 * 读中央目录里**未经 Qt 规范化**的条目名。
 *
 * QZipReader 会把 "../x" 静默改写成 "x"（等于悄悄纠正而不是拒绝），
 * 因此必须在交给它之前自己看原始字节，才能实现「出现 ../ 或绝对路径直接拒绝」。
 * 返回 false 时：error 为空表示这不是可解析的 zip（交给 QZipReader 报错），
 * 非空表示结构明确不支持 / 已损坏。
 */
bool rawZipEntryNames(const QByteArray &blob, QStringList *names, QString *error)
{
    constexpr int kEocdSize = 22;
    constexpr quint32 kEocdSignature = 0x06054b50u;
    constexpr quint32 kCentralSignature = 0x02014b50u;

    int eocd = -1;
    const int lowest = static_cast<int>(std::max<qsizetype>(0, blob.size() - kEocdSize - 65535));
    for (int i = blob.size() - kEocdSize; i >= lowest; --i) {
        if (readLe32(blob, i) == kEocdSignature) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return false;
    }

    const quint16 entryCount = readLe16(blob, eocd + 10);
    const quint32 centralSize = readLe32(blob, eocd + 12);
    const quint32 centralOffset = readLe32(blob, eocd + 16);
    if (entryCount == 0xFFFF || centralSize == 0xFFFFFFFFu || centralOffset == 0xFFFFFFFFu) {
        *error = QStringLiteral("技能包使用了 ZIP64 格式，暂不支持，请用普通 zip 重新打包");
        return false;
    }

    int cursor = static_cast<int>(centralOffset);
    for (int i = 0; i < entryCount; ++i) {
        if (cursor < 0 || cursor + 46 > blob.size()
            || readLe32(blob, cursor) != kCentralSignature) {
            *error = QStringLiteral("技能包中央目录损坏，无法校验条目路径");
            return false;
        }
        const quint16 nameLength = readLe16(blob, cursor + 28);
        const quint16 extraLength = readLe16(blob, cursor + 30);
        const quint16 commentLength = readLe16(blob, cursor + 32);
        if (cursor + 46 + nameLength > blob.size()) {
            *error = QStringLiteral("技能包中央目录损坏，无法校验条目路径");
            return false;
        }
        const QByteArray rawName = blob.mid(cursor + 46, nameLength);
        names->append(QString::fromUtf8(rawName));
        cursor += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

/// 目录内按大小写不敏感查找文件名；返回绝对路径，未命中返回空串
QString findFileCaseInsensitive(const QString &dirPath, const QString &fileName)
{
    const QDir dir(dirPath);
    const QStringList names = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        if (name.compare(fileName, Qt::CaseInsensitive) == 0) {
            return dir.filePath(name);
        }
    }
    return {};
}

bool removeRecursively(const QString &path)
{
    QDir dir(path);
    return !dir.exists() || dir.removeRecursively();
}

/// 把 src 目录内容复制到 dst（dst 必须不存在或为空目录）
bool copyDirectory(const QString &src, const QString &dst, QString *error)
{
    if (!QDir().mkpath(dst)) {
        *error = QStringLiteral("无法创建目录：%1").arg(dst);
        return false;
    }

    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    const QDir srcRoot(src);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString relative = srcRoot.relativeFilePath(filePath);
        const QString targetPath = QDir(dst).filePath(relative);
        const QString targetDir = QFileInfo(targetPath).absolutePath();
        if (!QDir().mkpath(targetDir)) {
            *error = QStringLiteral("无法创建目录：%1").arg(targetDir);
            return false;
        }
        if (!QFile::copy(filePath, targetPath)) {
            *error = QStringLiteral("无法写入文件：%1").arg(targetPath);
            return false;
        }
    }
    return true;
}

} // namespace

SkillInstaller::SkillInstaller(QString userRoot, qint64 maxPackageBytes,
                               qint64 maxExtractedBytes)
    : m_userRoot(std::move(userRoot))
    , m_maxPackageBytes(maxPackageBytes > 0 ? maxPackageBytes : kDefaultMaxPackageBytes)
    , m_maxExtractedBytes(maxExtractedBytes > 0 ? maxExtractedBytes : kDefaultMaxExtractedBytes)
{
}

QString SkillInstaller::userRoot() const
{
    return m_userRoot;
}

QString SkillInstaller::limitsText()
{
    return QStringLiteral("单包上限 20 MB，解压后上限 100 MB");
}

bool SkillInstaller::isSafeEntryName(const QString &entryName)
{
    const QString name = normalizedEntryName(entryName);
    if (name.isEmpty()) {
        return false;
    }
    if (name.startsWith(QLatin1Char('/'))) {
        return false;
    }
    // 盘符（C:/...）与 UNC / 协议式路径一律拒绝
    if (name.size() > 1 && name.at(1) == QLatin1Char(':')) {
        return false;
    }
    const QStringList segments = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        if (segment == QStringLiteral("..")) {
            return false;
        }
    }
    return !segments.isEmpty();
}

SkillImportResult SkillInstaller::install(const QString &zipPath,
                                          const QStringList &bundledIds) const
{
    SkillImportResult result;

#if !defined(MEOWAI_HAVE_QZIPREADER)
    Q_UNUSED(zipPath)
    Q_UNUSED(bundledIds)
    result.message = QStringLiteral("当前构建缺少 zip 支持，无法导入技能包");
    return result;
#else
    const QFileInfo packageInfo(zipPath);
    if (!packageInfo.exists() || !packageInfo.isFile()) {
        result.message = QStringLiteral("技能包不存在：%1").arg(zipPath);
        return result;
    }
    if (packageInfo.size() > m_maxPackageBytes) {
        result.message = QStringLiteral("技能包超过 %1 上限（当前 %2）")
                             .arg(formatMb(m_maxPackageBytes), formatMb(packageInfo.size()));
        return result;
    }

    // 先按中央目录的原始条目名做路径校验：QZipReader 会把 "../x" 静默改写成 "x"，
    // 只有看原始字节才能做到「拒绝」而不是「悄悄纠正」。
    QFile package(zipPath);
    if (!package.open(QIODevice::ReadOnly)) {
        result.message = QStringLiteral("无法读取技能包：%1").arg(zipPath);
        return result;
    }
    const QByteArray packageBytes = package.readAll();
    package.close();

    QStringList rawNames;
    QString rawError;
    if (rawZipEntryNames(packageBytes, &rawNames, &rawError)) {
        for (const QString &raw : rawNames) {
            if (!isSafeEntryName(raw)) {
                result.message =
                    QStringLiteral("技能包包含非法路径（目录穿越），已拒绝：%1").arg(raw);
                return result;
            }
        }
    } else if (!rawError.isEmpty()) {
        result.message = rawError;
        return result;
    }

    QZipReader reader(zipPath);
    if (!reader.isReadable()) {
        result.message = QStringLiteral("无法读取技能包：不是有效的 zip 文件");
        return result;
    }

    const QList<QZipReader::FileInfo> entries = reader.fileInfoList();
    if (entries.isEmpty()) {
        result.message = QStringLiteral("技能包是空的");
        return result;
    }
    if (entries.size() > kMaxEntries) {
        result.message = QStringLiteral("技能包条目过多（%1 个，上限 %2 个）")
                             .arg(entries.size())
                             .arg(kMaxEntries);
        return result;
    }

    QStringList manifestEntries;
    qint64 declaredBytes = 0;
    for (const QZipReader::FileInfo &entry : entries) {
        const QString name = normalizedEntryName(entry.filePath);
        if (name.isEmpty()) {
            continue;
        }
        if (!isSafeEntryName(name)) {
            result.message =
                QStringLiteral("技能包包含非法路径（目录穿越），已拒绝：%1").arg(name);
            return result;
        }
        if (entry.isSymLink) {
            result.message = QStringLiteral("技能包包含符号链接，已拒绝：%1").arg(name);
            return result;
        }
        if (entry.isDir) {
            continue;
        }
        declaredBytes += entry.size;
        if (declaredBytes > m_maxExtractedBytes) {
            result.message = QStringLiteral("解压后体积超过 %1 上限（当前 %2）")
                                 .arg(formatMb(m_maxExtractedBytes), formatMb(declaredBytes));
            return result;
        }
        if (name == QStringLiteral("manifest.json")
            || name.endsWith(QStringLiteral("/manifest.json"))) {
            manifestEntries << name;
        }
    }

    if (manifestEntries.isEmpty()) {
        result.message = QStringLiteral("技能包内未找到 manifest.json");
        return result;
    }
    if (manifestEntries.size() > 1) {
        result.message = QStringLiteral("技能包内存在多个 manifest.json，请只打包一个技能");
        return result;
    }

    const QString manifestEntry = manifestEntries.first();
    const QString prefix =
        manifestEntry.left(manifestEntry.size() - QStringLiteral("manifest.json").size());

    // 临时目录随作用域自动清理：失败路径不落半成品
    QTemporaryDir temp;
    if (!temp.isValid()) {
        result.message = QStringLiteral("无法创建临时目录，导入已取消");
        return result;
    }
    const QString skillDir = QDir(temp.path()).filePath(QStringLiteral("skill"));
    if (!QDir().mkpath(skillDir)) {
        result.message = QStringLiteral("无法创建临时目录，导入已取消");
        return result;
    }

    qint64 writtenBytes = 0;
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.isDir || entry.isSymLink) {
            continue;
        }
        const QString name = normalizedEntryName(entry.filePath);
        if (!name.startsWith(prefix)) {
            continue; // 只取 manifest 所在目录内的文件
        }
        const QString relative = name.mid(prefix.size());
        if (relative.isEmpty()) {
            continue;
        }
        if (!isSafeEntryName(relative)) {
            result.message =
                QStringLiteral("技能包包含非法路径（目录穿越），已拒绝：%1").arg(name);
            return result;
        }

        const QByteArray data = reader.fileData(entry.filePath);
        if (data.isNull() && entry.size > 0) {
            result.message = QStringLiteral("无法解压文件：%1").arg(name);
            return result;
        }
        writtenBytes += data.size();
        if (writtenBytes > m_maxExtractedBytes) {
            result.message = QStringLiteral("解压后体积超过 %1 上限（当前 %2）")
                                 .arg(formatMb(m_maxExtractedBytes), formatMb(writtenBytes));
            return result;
        }

        const QString targetPath = QDir(skillDir).filePath(relative);
        const QString targetDir = QFileInfo(targetPath).absolutePath();
        if (!QDir().mkpath(targetDir)) {
            result.message = QStringLiteral("无法创建目录：%1").arg(targetDir);
            return result;
        }
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.message = QStringLiteral("无法写入文件：%1").arg(relative);
            return result;
        }
        if (out.write(data) != data.size()) {
            result.message = QStringLiteral("写入不完整：%1").arg(relative);
            return result;
        }
    }

    SkillManifest manifest;
    QString reason;
    if (!SkillManifestRules::parseFile(QDir(skillDir).filePath(QStringLiteral("manifest.json")),
                                       &manifest, &reason)) {
        result.message = QStringLiteral("manifest 校验失败：%1").arg(reason);
        return result;
    }

    if (manifest.triggers.isEmpty()) {
        result.message = QStringLiteral(
                             "技能缺少 triggers（自动模式必需），已拒绝导入。正确示例：\n%1")
                             .arg(SkillManifestRules::exampleJson());
        return result;
    }

    if (manifest.entry.compare(QStringLiteral("skill.md"), Qt::CaseInsensitive) != 0) {
        result.message =
            QStringLiteral("manifest.entry 必须指向 skill.md（当前为 %1）").arg(manifest.entry);
        return result;
    }

    const QString documentPath = findFileCaseInsensitive(skillDir, QStringLiteral("skill.md"));
    if (documentPath.isEmpty()) {
        result.message = QStringLiteral("技能包缺少 skill.md");
        return result;
    }
    QFile document(documentPath);
    if (!document.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.message = QStringLiteral("无法读取 skill.md");
        return result;
    }
    if (QString::fromUtf8(document.readAll()).trimmed().isEmpty()) {
        result.message = QStringLiteral("skill.md 内容为空，已拒绝导入");
        return result;
    }

    if (bundledIds.contains(manifest.id)) {
        result.message = QStringLiteral(
                             "技能 id「%1」与内置技能冲突，请修改 manifest.id 后重试（例如 %1-custom）")
                             .arg(manifest.id);
        return result;
    }

    QDir root(m_userRoot);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        result.message = QStringLiteral("无法创建用户技能目录：%1").arg(m_userRoot);
        return result;
    }

    const QString target = root.filePath(manifest.id);
    const QString staging = root.filePath(QStringLiteral(".%1.importing").arg(manifest.id));
    removeRecursively(staging);

    QString copyError;
    if (!copyDirectory(skillDir, staging, &copyError)) {
        removeRecursively(staging);
        result.message = QStringLiteral("写入技能目录失败：%1").arg(copyError);
        return result;
    }

    const bool replaced = QFileInfo::exists(target);
    if (replaced && !removeRecursively(target)) {
        removeRecursively(staging);
        result.message = QStringLiteral("无法覆盖同名技能（目录可能被占用）：%1").arg(manifest.id);
        return result;
    }
    if (!QDir().rename(staging, target)) {
        removeRecursively(staging);
        result.message = QStringLiteral("安装技能失败：无法移动到 %1").arg(target);
        return result;
    }

    result.ok = true;
    result.skillId = manifest.id;
    result.message = replaced
                         ? QStringLiteral("已覆盖同名技能「%1」（%2）").arg(manifest.name, manifest.id)
                         : QStringLiteral("已导入技能「%1」（%2）").arg(manifest.name, manifest.id);
    return result;
#endif
}
