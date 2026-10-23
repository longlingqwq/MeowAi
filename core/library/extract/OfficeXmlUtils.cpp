#include "core/library/extract/OfficeXmlUtils.h"

#include "core/library/extract/Extractor.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <QtZlib/zlib.h>

#include <cstring>

namespace {

constexpr quint32 kEndOfCentralDirectorySignature = 0x06054b50u;
constexpr quint32 kCentralHeaderSignature = 0x02014b50u;
constexpr quint32 kLocalHeaderSignature = 0x04034b50u;

/// ZIP 中单个条目的最大文件名长度（规范上限）：EOCD 回溯窗口用得到
constexpr int kMaxZipComment = 65535;

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

quint16 readU16(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

/// 原始 deflate 流解压（windowBits = -15，不带 zlib/gzip 头尾）
bool inflateRaw(const QByteArray &input, quint32 expectedSize, QByteArray *output, QString *error)
{
    output->clear();
    if (expectedSize == 0) {
        return true;  // 空条目
    }

    output->resize(int(expectedSize));

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = uInt(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output->data());
    stream.avail_out = uInt(output->size());

    // Qt 自带的 zlib 已把符号前缀化为 z_*，这里走 zlib 官方文档给出的 `z_inflateInit2` 宏
    if (z_inflateInit2(&stream, -15) != Z_OK) {
        setError(error, QStringLiteral("zlib 初始化失败，无法解压 Office 内容"));
        return false;
    }

    const int status = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);

    if (status != Z_STREAM_END) {
        output->clear();
        setError(error, QStringLiteral("Office 内容解压失败（压缩数据损坏）"));
        return false;
    }
    output->resize(int(produced));
    return true;
}

}  // namespace

bool OfficeZipArchive::open(const QString &path, QString *error)
{
    m_bytes.clear();
    m_entries.clear();

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        setError(error, QStringLiteral("文件不存在或不是普通文件：%1").arg(path));
        return false;
    }
    if (!validateExtractSize(info.size(), error)) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法读取文件：%1").arg(path));
        return false;
    }
    m_bytes = file.readAll();
    file.close();

    const int size = m_bytes.size();
    if (size < 22) {
        setError(error, QStringLiteral("不是有效的 Office 文件（容器过小）"));
        return false;
    }

    // 从尾部回溯 EOCD：注释最长 64KB，故只需回看这么多
    int eocdOffset = -1;
    const int scanFrom = qMax(0, size - 22 - kMaxZipComment);
    for (int i = size - 22; i >= scanFrom; --i) {
        if (readU32(m_bytes, i) != kEndOfCentralDirectorySignature) {
            continue;
        }
        // 校验「注释长度」与文件尾一致，避免误命中数据里的同名字节串
        const int commentLength = readU16(m_bytes, i + 20);
        if (i + 22 + commentLength == size) {
            eocdOffset = i;
            break;
        }
    }
    if (eocdOffset < 0) {
        setError(error, QStringLiteral("不是有效的 Office 文件（未找到 ZIP 中央目录）"));
        return false;
    }

    const int totalEntries = readU16(m_bytes, eocdOffset + 10);
    int offset = int(readU32(m_bytes, eocdOffset + 16));

    for (int i = 0; i < totalEntries; ++i) {
        if (offset < 0 || offset + 46 > size || readU32(m_bytes, offset) != kCentralHeaderSignature) {
            setError(error, QStringLiteral("Office 文件结构损坏（中央目录第 %1 项异常）").arg(i + 1));
            return false;
        }
        Entry entry;
        entry.method = readU16(m_bytes, offset + 10);
        entry.compressedSize = readU32(m_bytes, offset + 20);
        entry.uncompressedSize = readU32(m_bytes, offset + 24);
        const quint16 nameLength = readU16(m_bytes, offset + 28);
        const quint16 extraLength = readU16(m_bytes, offset + 30);
        const quint16 commentLength = readU16(m_bytes, offset + 32);
        entry.localHeaderOffset = readU32(m_bytes, offset + 42);

        if (offset + 46 + nameLength > size) {
            setError(error, QStringLiteral("Office 文件结构损坏（条目名越界）"));
            return false;
        }
        const QString name = QString::fromUtf8(m_bytes.constData() + offset + 46, nameLength);
        m_entries.insert(name, entry);

        offset += 46 + nameLength + extraLength + commentLength;
    }

    if (m_entries.isEmpty()) {
        setError(error, QStringLiteral("Office 文件为空（没有任何条目）"));
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool OfficeZipArchive::isOpen() const
{
    return !m_entries.isEmpty();
}

QStringList OfficeZipArchive::names() const
{
    return m_entries.keys();
}

bool OfficeZipArchive::contains(const QString &entryName) const
{
    return m_entries.contains(entryName);
}

bool OfficeZipArchive::read(const QString &entryName, QByteArray *data, QString *error) const
{
    if (data) {
        data->clear();
    }
    if (!isOpen()) {
        setError(error, QStringLiteral("Office 容器尚未打开"));
        return false;
    }

    const auto it = m_entries.constFind(entryName);
    if (it == m_entries.constEnd()) {
        setError(error, QStringLiteral("Office 文件中缺少条目：%1").arg(entryName));
        return false;
    }
    const Entry &entry = it.value();

    // 本地头的「额外字段长度」可能与中央目录不同，必须按本地头重新计算数据起点
    const int localOffset = int(entry.localHeaderOffset);
    if (localOffset < 0 || localOffset + 30 > m_bytes.size()
        || readU32(m_bytes, localOffset) != kLocalHeaderSignature) {
        setError(error, QStringLiteral("Office 文件结构损坏（条目 %1 本地头异常）").arg(entryName));
        return false;
    }
    const quint16 localNameLength = readU16(m_bytes, localOffset + 26);
    const quint16 localExtraLength = readU16(m_bytes, localOffset + 28);
    const int dataOffset = localOffset + 30 + localNameLength + localExtraLength;
    if (dataOffset + int(entry.compressedSize) > m_bytes.size()) {
        setError(error, QStringLiteral("Office 文件结构损坏（条目 %1 数据越界）").arg(entryName));
        return false;
    }

    const QByteArray compressed = m_bytes.mid(dataOffset, int(entry.compressedSize));
    switch (entry.method) {
    case 0:  // stored
        if (data) {
            *data = compressed;
        }
        if (error) {
            error->clear();
        }
        return true;
    case 8:  // deflate
        if (!inflateRaw(compressed, entry.uncompressedSize, data, error)) {
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    default:
        setError(error, QStringLiteral("不支持的压缩方式（%1），仅支持 stored / deflate")
                            .arg(entry.method));
        return false;
    }
}

QString collapseXmlWhitespace(const QString &text)
{
    QString collapsed = text.simplified();
    return collapsed;
}
