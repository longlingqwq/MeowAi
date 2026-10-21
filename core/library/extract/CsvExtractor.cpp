#include "core/library/extract/CsvExtractor.h"

#include <algorithm>

namespace {

/// RFC 4180 风格解析：支持引号包裹、引号内的分隔符与换行、`""` 转义
QList<QStringList> parseDelimitedRows(const QString &text, QChar delimiter)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool inQuotes = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field.append(QLatin1Char('"'));
                    ++i;  // 跳过转义引号
                } else {
                    inQuotes = false;
                }
            } else {
                field.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('"') && field.isEmpty()) {
            inQuotes = true;  // 仅字段起始处的引号才视为包裹
            continue;
        }
        if (ch == delimiter) {
            row.append(field);
            field.clear();
            continue;
        }
        if (ch == QLatin1Char('\n')) {
            row.append(field);
            field.clear();
            rows.append(row);
            row.clear();
            continue;
        }
        field.append(ch);
    }
    if (!field.isEmpty() || !row.isEmpty()) {  // 文件末行无换行时的收尾
        row.append(field);
        rows.append(row);
    }
    return rows;
}

/// 整行皆空（通常是文件中的空行）——不作为数据行
bool isBlankRow(const QStringList &row)
{
    if (row.size() != 1) {
        return false;
    }
    return row.first().trimmed().isEmpty();
}

}  // namespace

QString CsvExtractor::id() const
{
    return QStringLiteral("csv");
}

bool CsvExtractor::supports(LibraryKind kind, const QString &extension) const
{
    return kind == LibraryKind::Text
           && (extension == QStringLiteral("csv") || extension == QStringLiteral("tsv"));
}

bool CsvExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }

    QString text;
    if (!readLibraryTextFile(path, &text, error)) {
        return false;
    }

    const QChar delimiter = libraryExtension(path) == QStringLiteral("tsv") ? QLatin1Char('\t')
                                                                            : QLatin1Char(',');
    QList<QStringList> rows = parseDelimitedRows(text, delimiter);
    rows.erase(std::remove_if(rows.begin(), rows.end(), isBlankRow), rows.end());

    if (rows.isEmpty()) {
        if (result) {
            result->ok = true;  // 空文件合法：入库但无片段
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    const QStringList header = rows.first();

    if (rows.size() == 1) {
        // 只有表头：仍需给出可核对的一行片段
        ExtractedSegment segment;
        segment.text = markdownRow(header, header.size());
        segment.locKind = LocatorKind::Row;
        segment.locRowFrom = 1;
        segment.locRowTo = 1;
        segment.locLabel = QStringLiteral("第 1 行");
        if (result) {
            result->segments.append(segment);
            result->ok = true;
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    const int dataCount = rows.size() - 1;
    for (int start = 0; start < dataCount; start += kTableRowsPerSegment) {
        const int count = qMin(kTableRowsPerSegment, dataCount - start);

        int columns = header.size();
        for (int i = 0; i < count; ++i) {
            columns = qMax(columns, rows.at(start + 1 + i).size());
        }

        QStringList lines;
        lines.append(markdownRow(header, columns));
        lines.append(markdownSeparator(columns));
        for (int i = 0; i < count; ++i) {
            lines.append(markdownRow(rows.at(start + 1 + i), columns));
        }

        ExtractedSegment segment;
        segment.text = lines.join(QLatin1Char('\n'));
        segment.locKind = LocatorKind::Row;
        segment.locRowFrom = start + 2;          // 表头占第 1 行
        segment.locRowTo = start + 1 + count;
        segment.locLabel = count == 1
                               ? QStringLiteral("第 %1 行").arg(segment.locRowFrom)
                               : QStringLiteral("第 %1-%2 行")
                                     .arg(segment.locRowFrom)
                                     .arg(segment.locRowTo);
        if (result) {
            result->segments.append(segment);
        }
    }

    if (result) {
        result->ok = true;
    }
    if (error) {
        error->clear();
    }
    return true;
}
