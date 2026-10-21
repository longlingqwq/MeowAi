#include "core/library/extract/XlsxExtractor.h"

#include "core/library/extract/OfficeXmlUtils.h"

#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>

namespace {

const QString kWorkbookEntry = QStringLiteral("xl/workbook.xml");
const QString kWorkbookRelsEntry = QStringLiteral("xl/_rels/workbook.xml.rels");
const QString kSharedStringsEntry = QStringLiteral("xl/sharedStrings.xml");

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

/// 单元格引用（如 `B12`）的列字母 → 0 起列号；无法识别返回 -1
int columnIndexOfRef(const QString &ref)
{
    int value = 0;
    int letters = 0;
    for (const QChar ch : ref) {
        if (!ch.isLetter()) {
            break;
        }
        value = value * 26 + (ch.toUpper().unicode() - 'A' + 1);
        ++letters;
    }
    if (letters == 0) {
        return -1;
    }
    return value - 1;
}

/// `xl/_rels/workbook.xml.rels`：`rId` → 目标路径
QHash<QString, QString> readWorkbookRelationships(const OfficeZipArchive &archive)
{
    QHash<QString, QString> targets;
    QByteArray xml;
    if (!archive.read(kWorkbookRelsEntry, &xml, nullptr)) {
        return targets;
    }

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && officeElementName(reader) == QStringLiteral("Relationship")) {
            const QString id = officeAttribute(reader, QStringLiteral("Id"));
            const QString target = officeAttribute(reader, QStringLiteral("Target"));
            if (!id.isEmpty() && !target.isEmpty()) {
                targets.insert(id, target);
            }
        }
    }
    return targets;
}

/// 关系里的相对路径（`worksheets/sheet1.xml`）解析到包内绝对条目名
QString resolveWorksheetEntry(const QString &target)
{
    QString path = target;
    if (path.startsWith(QLatin1Char('/'))) {
        return path.mid(1);  // 包内绝对路径
    }
    path.prepend(QStringLiteral("xl/"));
    QStringList parts;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QStringLiteral("..")) {
            if (!parts.isEmpty()) {
                parts.removeLast();
            }
        } else if (part != QStringLiteral(".")) {
            parts.append(part);
        }
    }
    return parts.join(QLatin1Char('/'));
}

/// 共享字符串表：每个 `<si>` 内的文本拼接为一条
QStringList readSharedStrings(const OfficeZipArchive &archive)
{
    QStringList strings;
    QByteArray xml;
    if (!archive.read(kSharedStringsEntry, &xml, nullptr)) {
        return strings;
    }

    QXmlStreamReader reader(xml);
    QString current;
    bool collecting = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("si")) {
                current.clear();
            } else if (name == QStringLiteral("t")) {
                collecting = true;
            }
        } else if (reader.isCharacters()) {
            if (collecting) {
                current += reader.text().toString();
            }
        } else if (reader.isEndElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("t")) {
                collecting = false;
            } else if (name == QStringLiteral("si")) {
                strings.append(collapseXmlWhitespace(current));
            }
        }
    }
    return strings;
}

struct SheetRow
{
    int number = 0;         ///< 工作表内的真实行号（`row/@r`）
    QStringList cells;      ///< 按列号放置的单元格值
};

/// 解析单张工作表 XML
QList<SheetRow> readSheetRows(const QByteArray &xml, const QStringList &sharedStrings)
{
    QList<SheetRow> rows;
    QXmlStreamReader reader(xml);

    SheetRow row;
    QStringList cells;
    QString cellType;
    QString inlineText;
    QString valueText;
    int column = -1;
    bool inValue = false;
    bool inInlineText = false;
    int autoRowNumber = 0;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isCharacters()) {
            if (inValue) {
                valueText += reader.text().toString();
            } else if (inInlineText) {
                inlineText += reader.text().toString();
            }
            continue;
        }

        if (reader.isStartElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("row")) {
                ++autoRowNumber;
                cells.clear();
                const int declared = officeAttribute(reader, QStringLiteral("r")).toInt();
                row.number = declared > 0 ? declared : autoRowNumber;
                row.cells.clear();
            } else if (name == QStringLiteral("c")) {
                column = columnIndexOfRef(officeAttribute(reader, QStringLiteral("r")));
                cellType = officeAttribute(reader, QStringLiteral("t"));
                inlineText.clear();
                valueText.clear();
            } else if (name == QStringLiteral("v")) {
                inValue = true;
            } else if (name == QStringLiteral("t")) {
                inInlineText = true;
            }
            continue;
        }

        if (reader.isEndElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("v")) {
                inValue = false;
            } else if (name == QStringLiteral("t")) {
                inInlineText = false;
            } else if (name == QStringLiteral("c")) {
                QString value;
                if (cellType == QStringLiteral("s")) {
                    bool ok = false;
                    const int index = collapseXmlWhitespace(valueText).toInt(&ok);
                    if (ok && index >= 0 && index < sharedStrings.size()) {
                        value = sharedStrings.at(index);
                    }
                } else if (cellType == QStringLiteral("inlineStr")) {
                    value = collapseXmlWhitespace(inlineText);
                } else {
                    value = collapseXmlWhitespace(valueText);
                }
                if (column >= 0) {
                    while (cells.size() <= column) {
                        cells.append(QString());
                    }
                    cells[column] = value;
                } else if (!value.isEmpty()) {
                    cells.append(value);
                }
                column = -1;
            } else if (name == QStringLiteral("row")) {
                row.cells = cells;
                rows.append(row);
                row = SheetRow();
                cells.clear();
            }
        }
    }

    // 整行皆空的行不参与建表（Excel 常在数据区留空行）
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const SheetRow &item) {
                                  for (const QString &cell : item.cells) {
                                      if (!cell.trimmed().isEmpty()) {
                                          return false;
                                      }
                                  }
                                  return true;
                              }),
               rows.end());
    return rows;
}

/// 把一张工作表的行转成片段（首个非空行为表头，每片段最多 20 条数据行）
void appendSheetSegments(const QString &sheetName, const QList<SheetRow> &rows,
                         QList<ExtractedSegment> *segments)
{
    if (rows.isEmpty()) {
        return;
    }

    const auto buildSegment = [&](const QString &text, int rowFrom, int rowTo) {
        ExtractedSegment segment;
        segment.text = text;
        segment.locKind = LocatorKind::Sheet;
        segment.locSheet = sheetName;
        segment.locRowFrom = rowFrom;
        segment.locRowTo = rowTo;
        segment.locLabel = rowFrom == rowTo
                               ? QStringLiteral("%1 第 %2 行").arg(sheetName).arg(rowFrom)
                               : QStringLiteral("%1 第 %2-%3 行")
                                     .arg(sheetName)
                                     .arg(rowFrom)
                                     .arg(rowTo);
        segments->append(segment);
    };

    const SheetRow &headerRow = rows.first();
    if (rows.size() == 1) {
        buildSegment(markdownRow(headerRow.cells, headerRow.cells.size()),
                     headerRow.number, headerRow.number);
        return;
    }

    const int dataCount = rows.size() - 1;
    for (int start = 0; start < dataCount; start += kTableRowsPerSegment) {
        const int count = qMin(kTableRowsPerSegment, dataCount - start);

        int columns = headerRow.cells.size();
        for (int i = 0; i < count; ++i) {
            columns = qMax(columns, rows.at(start + 1 + i).cells.size());
        }

        QStringList lines;
        lines.append(markdownRow(headerRow.cells, columns));
        lines.append(markdownSeparator(columns));
        for (int i = 0; i < count; ++i) {
            lines.append(markdownRow(rows.at(start + 1 + i).cells, columns));
        }

        buildSegment(lines.join(QLatin1Char('\n')), rows.at(start + 1).number,
                     rows.at(start + count).number);
    }
}

}  // namespace

QString XlsxExtractor::id() const
{
    return QStringLiteral("xlsx");
}

bool XlsxExtractor::supports(LibraryKind kind, const QString &extension) const
{
    return kind == LibraryKind::Text && extension == QStringLiteral("xlsx");
}

bool XlsxExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }

    OfficeZipArchive archive;
    if (!archive.open(path, error)) {
        return false;
    }
    if (!archive.contains(kWorkbookEntry)) {
        setError(error, QStringLiteral("不是有效的 xlsx（缺少 %1）").arg(kWorkbookEntry));
        return false;
    }

    QByteArray workbookXml;
    if (!archive.read(kWorkbookEntry, &workbookXml, error)) {
        return false;
    }

    const QHash<QString, QString> relationships = readWorkbookRelationships(archive);
    const QStringList sharedStrings = readSharedStrings(archive);

    // workbook.xml 的 <sheet> 顺序即用户看到的顺序，按它逐表处理
    QStringList sheetNames;
    QStringList sheetEntries;
    {
        QXmlStreamReader reader(workbookXml);
        while (!reader.atEnd()) {
            reader.readNext();
            if (!reader.isStartElement() || officeElementName(reader) != QStringLiteral("sheet")) {
                continue;
            }
            const QString name = officeAttribute(reader, QStringLiteral("name"));
            const QString relationshipId = officeAttribute(reader, QStringLiteral("id"));
            if (name.isEmpty() || relationshipId.isEmpty()) {
                continue;
            }
            const QString target = relationships.value(relationshipId);
            if (target.isEmpty()) {
                continue;
            }
            sheetNames.append(name);
            sheetEntries.append(resolveWorksheetEntry(target));
        }
    }

    if (sheetEntries.isEmpty()) {
        setError(error, QStringLiteral("xlsx 中没有可读取的工作表"));
        return false;
    }

    QList<ExtractedSegment> segments;
    QStringList missingSheets;
    for (int i = 0; i < sheetEntries.size(); ++i) {
        QByteArray sheetXml;
        if (!archive.read(sheetEntries.at(i), &sheetXml, nullptr)) {
            // 单表缺失不中断整体解析，但要如实报告
            missingSheets.append(sheetNames.at(i));
            continue;
        }
        appendSheetSegments(sheetNames.at(i), readSheetRows(sheetXml, sharedStrings), &segments);
    }

    if (segments.isEmpty() && !missingSheets.isEmpty()) {
        setError(error, QStringLiteral("xlsx 的工作表内容无法读取：%1")
                            .arg(missingSheets.join(QStringLiteral("、"))));
        return false;
    }

    if (result) {
        result->segments = segments;
        result->ok = true;
    }
    if (error) {
        error->clear();
    }
    return true;
}
