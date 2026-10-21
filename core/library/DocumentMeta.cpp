#include "core/library/DocumentMeta.h"

#include <QHash>
#include <algorithm>

namespace {

/// 按标题划分章节；标题之前的片段归入「文档开头」
DocumentMeta withHeadings(const QList<ChunkRecord> &chunks, const QList<HeadingSpan> &headings)
{
    DocumentMeta meta;
    meta.sectionSource = QStringLiteral("heading");

    const int firstOrdinal = chunks.first().ordinal;
    const int lastOrdinal = chunks.last().ordinal;

    QHash<int, int> tokensByOrdinal;
    tokensByOrdinal.reserve(chunks.size());
    for (const ChunkRecord &chunk : chunks) {
        tokensByOrdinal.insert(chunk.ordinal, chunk.tokenEst);
    }

    const auto rangeTokens = [&tokensByOrdinal](int from, int to) {
        int total = 0;
        for (int ordinal = from; ordinal <= to; ++ordinal) {
            total += tokensByOrdinal.value(ordinal, 0);
        }
        return total;
    };

    if (headings.first().ordinal > firstOrdinal) {
        DetectedSection preamble;
        preamble.title = QStringLiteral("文档开头");
        preamble.chunkStart = firstOrdinal;
        preamble.chunkEnd = headings.first().ordinal - 1;
        preamble.tokenEst = rangeTokens(preamble.chunkStart, preamble.chunkEnd);
        meta.sections.append(preamble);
    }

    for (int i = 0; i < headings.size(); ++i) {
        const int start = qMax(firstOrdinal, headings.at(i).ordinal);
        const int end = (i + 1 < headings.size()) ? headings.at(i + 1).ordinal - 1 : lastOrdinal;
        if (end < start) {
            continue;
        }
        DetectedSection section;
        section.title = headings.at(i).title.isEmpty() ? QStringLiteral("未命名章节")
                                                        : headings.at(i).title;
        section.chunkStart = start;
        section.chunkEnd = end;
        section.tokenEst = rangeTokens(start, end);
        meta.sections.append(section);
    }
    return meta;
}

/// 取出现次数最多的定位类型作为分组线索
QString dominantLocKind(const QList<ChunkRecord> &chunks)
{
    QHash<QString, int> counts;
    for (const ChunkRecord &chunk : chunks) {
        if (!chunk.locKind.isEmpty()) {
            ++counts[chunk.locKind];
        }
    }

    QString primary;
    int maxCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            primary = it.key();
        }
    }
    return primary;
}

/// 一页 / 一行 / 一个分支一章：每个片段自成章节
QList<DetectedSection> oneChunkPerSection(const QList<ChunkRecord> &chunks,
                                           const QString &titleTemplate)
{
    QList<DetectedSection> sections;
    sections.reserve(chunks.size());
    for (const ChunkRecord &chunk : chunks) {
        DetectedSection section;
        section.title = titleTemplate.isEmpty()
                            ? (chunk.locLabel.isEmpty() ? QStringLiteral("未命名章节") : chunk.locLabel)
                            : titleTemplate.arg(chunk.locPage > 0 ? chunk.locPage : chunk.ordinal + 1);
        section.chunkStart = chunk.ordinal;
        section.chunkEnd = chunk.ordinal;
        section.tokenEst = chunk.tokenEst;
        sections.append(section);
    }
    return sections;
}

/// 按某个字段把连续片段分组（工作表名）
QList<DetectedSection> groupBySheet(const QList<ChunkRecord> &chunks)
{
    QList<DetectedSection> sections;
    QList<ChunkRecord> group;
    QString currentSheet;

    const auto flush = [&sections, &group, &currentSheet]() {
        if (group.isEmpty()) {
            return;
        }
        DetectedSection section;
        section.title = currentSheet.isEmpty() ? QStringLiteral("未命名工作表") : currentSheet;
        section.chunkStart = group.first().ordinal;
        section.chunkEnd = group.last().ordinal;
        for (const ChunkRecord &chunk : group) {
            section.tokenEst += chunk.tokenEst;
        }
        sections.append(section);
        group.clear();
    };

    for (const ChunkRecord &chunk : chunks) {
        if (chunk.locSheet != currentSheet) {
            flush();
            currentSheet = chunk.locSheet;
        }
        group.append(chunk);
    }
    flush();
    return sections;
}

}  // namespace

DocumentMeta detectDocumentMeta(const QList<ChunkRecord> &chunks, const QList<HeadingSpan> &headings)
{
    DocumentMeta meta;
    if (chunks.isEmpty()) {
        return meta;
    }

    for (const ChunkRecord &chunk : chunks) {
        meta.tokenEstTotal += chunk.tokenEst;
    }

    // 1. 解析层识别出的标题优先级最高
    if (!headings.isEmpty()) {
        QList<HeadingSpan> ordered = headings;
        std::sort(ordered.begin(), ordered.end(),
                  [](const HeadingSpan &a, const HeadingSpan &b) { return a.ordinal < b.ordinal; });
        meta = withHeadings(chunks, ordered);
        meta.tokenEstTotal = 0;
        for (const ChunkRecord &chunk : chunks) {
            meta.tokenEstTotal += chunk.tokenEst;
        }
        return meta;
    }

    // 2. 按主定位类型划分
    const QString primaryKind = dominantLocKind(chunks);
    if (primaryKind == QStringLiteral("sheet")) {
        meta.sectionSource = QStringLiteral("sheet");
        meta.sections = groupBySheet(chunks);
    } else if (primaryKind == QStringLiteral("slide")) {
        meta.sectionSource = QStringLiteral("slide");
        meta.sections = oneChunkPerSection(chunks, QStringLiteral("第 %1 页"));
    } else if (primaryKind == QStringLiteral("page")) {
        meta.sectionSource = QStringLiteral("page_approx");
        meta.sections = oneChunkPerSection(chunks, QStringLiteral("第 %1 页"));
    } else if (primaryKind == QStringLiteral("json")) {
        meta.sectionSource = QStringLiteral("page_approx");
        meta.sections = oneChunkPerSection(chunks, QString());
    } else {
        // para / row / image / none：不做章节
        meta.sectionSource = QStringLiteral("none");
    }

    return meta;
}
