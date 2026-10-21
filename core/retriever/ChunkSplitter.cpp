#include "core/retriever/ChunkSplitter.h"

#include <QChar>
#include <QStringList>

#include <algorithm>

namespace {

/// 表格类片段：CSV 的行区间、XLSX 的工作表行区间（文本均为 Markdown 表格）
bool isTableSegment(const ExtractedSegment &segment)
{
    return segment.locKind == LocatorKind::Row || segment.locKind == LocatorKind::Sheet;
}

/// 统一换行并去掉首尾空白；空文本返回空串
QString normalizeText(const QString &text)
{
    QString value = text;
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return value.trimmed();
}

/// 在 [floorPos, hardEnd] 内找一个「尽量靠后且不切断句子」的切点；找不到返回 hardEnd
int boundaryCut(const QString &text, int floorPos, int hardEnd)
{
    static const QStringList marks{
        QStringLiteral("\n\n"), QStringLiteral("\n"), QStringLiteral("。"),
        QStringLiteral("！"),   QStringLiteral("？"), QStringLiteral("；"),
        QStringLiteral(". "),   QStringLiteral("! "), QStringLiteral("? "),
        QStringLiteral("; "),
    };

    int best = -1;
    for (const QString &mark : marks) {
        const int index = text.lastIndexOf(mark, hardEnd - 1);
        if (index < floorPos || index + mark.size() > hardEnd) {
            continue;
        }
        best = std::max(best, static_cast<int>(index + mark.size()));
    }
    return best > floorPos ? best : hardEnd;
}

/// Markdown 表格的定位串：优先按「工作表 + 行区间」重算，保持与解析层同一口径
QString tableLabel(const ExtractedSegment &segment, int rowFrom, int rowTo)
{
    if (rowFrom <= 0 || rowTo <= 0) {
        return segment.locLabel;
    }
    const QString range = rowFrom == rowTo ? QStringLiteral("第 %1 行").arg(rowFrom)
                                           : QStringLiteral("第 %1-%2 行").arg(rowFrom).arg(rowTo);
    if (segment.locKind == LocatorKind::Sheet && !segment.locSheet.isEmpty()) {
        return QStringLiteral("%1 %2").arg(segment.locSheet, range);
    }
    return range;
}

}  // namespace

ChunkSplitOptions ChunkSplitOptions::normalized() const
{
    ChunkSplitOptions result;
    result.chunkSize = std::max(40, chunkSize);
    result.overlap = std::max(0, std::min(overlap, result.chunkSize / 2));
    return result;
}

int estimateTokens(const QString &text)
{
    int wide = 0;   // CJK / 全角字符：约 1 字 1 token
    int narrow = 0; // 其余字符：约 4 字符 1 token
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        const bool isWide = (code >= 0x3040 && code <= 0x30FF)   // 日文假名
                            || (code >= 0x3400 && code <= 0x4DBF)  // 扩展 A
                            || (code >= 0x4E00 && code <= 0x9FFF)  // CJK 统一表意
                            || (code >= 0xAC00 && code <= 0xD7AF)  // 谚文
                            || (code >= 0xF900 && code <= 0xFAFF)  // 兼容表意
                            || (code >= 0xFF00 && code <= 0xFFEF); // 全角
        if (isWide) {
            ++wide;
        } else {
            ++narrow;
        }
    }
    return wide + (narrow + 3) / 4;
}

ChunkSplitter::ChunkSplitter(ChunkSplitOptions options)
    : m_options(options.normalized())
{
}

ChunkSplitOptions ChunkSplitter::options() const
{
    return m_options;
}

QList<ChunkRecord> ChunkSplitter::split(qint64 fileId,
                                         const QList<ExtractedSegment> &segments) const
{
    QList<ChunkRecord> chunks;
    int ordinal = 0;
    for (const ExtractedSegment &segment : segments) {
        if (normalizeText(segment.text).isEmpty()) {
            continue;  // 空片段不入库，避免产生无意义的零字块
        }
        if (isTableSegment(segment)) {
            splitTable(fileId, segment, &chunks, &ordinal);
        } else {
            splitText(fileId, segment, &chunks, &ordinal);
        }
    }
    return chunks;
}

ChunkRecord ChunkSplitter::makeChunk(qint64 fileId, int ordinal, const QString &text,
                                      const ExtractedSegment &segment) const
{
    ChunkRecord chunk;
    chunk.fileId = fileId;
    chunk.ordinal = ordinal;
    chunk.text = text;
    chunk.locKind = locatorKindName(segment.locKind);
    chunk.locLabel = segment.locLabel;
    chunk.locPage = segment.locPage;
    chunk.locSheet = segment.locSheet;
    chunk.locRowFrom = segment.locRowFrom;
    chunk.locRowTo = segment.locRowTo;
    chunk.charCount = text.size();
    chunk.tokenEst = estimateTokens(text);
    return chunk;
}

void ChunkSplitter::splitText(qint64 fileId, const ExtractedSegment &segment,
                               QList<ChunkRecord> *out, int *ordinal) const
{
    const QString text = normalizeText(segment.text);
    const int length = text.size();
    const int size = m_options.chunkSize;
    const int overlap = m_options.overlap;

    int pos = 0;
    while (pos < length) {
        int end = std::min(pos + size, length);
        if (end < length) {
            // 切点尽量落在段落 / 句末，避免把一句话拦腰截断；
            // 下限取目标尺寸的 3/5，防止切点太靠前导致片段过碎
            end = boundaryCut(text, pos + size * 3 / 5, end);
        }

        const QString piece = text.mid(pos, end - pos).trimmed();
        if (!piece.isEmpty()) {
            out->append(makeChunk(fileId, *ordinal, piece, segment));
            ++(*ordinal);
        }

        if (end >= length) {
            break;
        }
        pos = std::max(end - overlap, pos + 1);
    }
}

void ChunkSplitter::splitTable(qint64 fileId, const ExtractedSegment &segment,
                                QList<ChunkRecord> *out, int *ordinal) const
{
    const QString text = normalizeText(segment.text);
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return;
    }

    // Markdown 表格的前两行是表头 + 分隔行；两者在每块中重复，保证脱离上下文仍可读
    QStringList headerLines;
    int firstRow = 0;
    if (lines.size() >= 2 && lines.at(0).trimmed().startsWith(QLatin1Char('|'))
        && lines.at(1).contains(QStringLiteral("---"))) {
        headerLines = {lines.at(0), lines.at(1)};
        firstRow = 2;
    }

    const QString prefix = headerLines.isEmpty() ? QString() : headerLines.join(QLatin1Char('\n'));
    const int prefixSize = prefix.isEmpty() ? 0 : prefix.size() + 1;
    const int budget = std::max(0, m_options.chunkSize - prefixSize);
    const int baseRow = segment.locRowFrom;

    // 只有表头、没有数据行：仍保留一块可核对的表头，避免整段消失
    if (firstRow >= lines.size()) {
        out->append(makeChunk(fileId, *ordinal, prefix, segment));
        ++(*ordinal);
        return;
    }

    int start = firstRow;
    while (start < lines.size()) {
        int end = start;
        int used = 0;
        while (end < lines.size()) {
            const int lineSize = lines.at(end).size() + 1;
            // 至少容纳一行：极宽的行即使超预算也不单独丢弃
            if (end > start && used + lineSize > budget) {
                break;
            }
            used += lineSize;
            ++end;
        }

        QStringList content;
        if (!prefix.isEmpty()) {
            content.append(prefix);
        }
        for (int i = start; i < end; ++i) {
            content.append(lines.at(i));
        }

        ExtractedSegment locator = segment;
        if (baseRow > 0) {
            const int rowFrom = baseRow + (start - firstRow);
            const int rowTo = baseRow + (end - firstRow) - 1;
            locator.locRowFrom = rowFrom;
            locator.locRowTo = rowTo;
            locator.locLabel = tableLabel(segment, rowFrom, rowTo);
        }

        out->append(makeChunk(fileId, *ordinal, content.join(QLatin1Char('\n')), locator));
        ++(*ordinal);
        start = end;
    }
}
