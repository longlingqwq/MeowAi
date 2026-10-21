#include "core/library/extract/DocxExtractor.h"

#include "core/library/extract/OfficeXmlUtils.h"

#include <QXmlStreamReader>

namespace {

const QString kDocumentEntry = QStringLiteral("word/document.xml");

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

}  // namespace

QString DocxExtractor::id() const
{
    return QStringLiteral("docx");
}

bool DocxExtractor::supports(LibraryKind kind, const QString &extension) const
{
    return kind == LibraryKind::Text && extension == QStringLiteral("docx");
}

bool DocxExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }

    OfficeZipArchive archive;
    if (!archive.open(path, error)) {
        return false;
    }
    if (!archive.contains(kDocumentEntry)) {
        setError(error, QStringLiteral("不是有效的 docx（缺少 %1）").arg(kDocumentEntry));
        return false;
    }

    QByteArray xml;
    if (!archive.read(kDocumentEntry, &xml, error)) {
        return false;
    }

    QXmlStreamReader reader(xml);
    QList<ExtractedSegment> segments;
    int ordinal = 0;

    QString paragraph;    // 正文段落累积
    QString cellText;     // 表格单元格累积
    QStringList rowCells;
    bool inText = false;  // 仅收集 <w:t> 内的字符
    bool inTable = false;

    const auto emitSegment = [&segments, &ordinal](const QString &body) {
        const QString text = collapseXmlWhitespace(body);
        if (text.isEmpty()) {
            return;
        }
        ++ordinal;
        ExtractedSegment segment;
        segment.text = text;
        segment.locKind = LocatorKind::Para;
        segment.locLabel = QStringLiteral("第 %1 段").arg(ordinal);
        segments.append(segment);
    };

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isCharacters()) {
            if (!inText) {
                continue;
            }
            if (inTable) {
                cellText += reader.text().toString();
            } else {
                paragraph += reader.text().toString();
            }
            continue;
        }

        if (reader.isStartElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("t")) {
                inText = true;
            } else if (name == QStringLiteral("tbl")) {
                inTable = true;
            } else if (name == QStringLiteral("tr")) {
                rowCells.clear();
            } else if (name == QStringLiteral("tc")) {
                cellText.clear();
            } else if (name == QStringLiteral("tab") || name == QStringLiteral("br")) {
                // 制表符 / 换行按空白处理，避免相邻文本被粘连
                if (inTable) {
                    cellText += QLatin1Char(' ');
                } else {
                    paragraph += QLatin1Char(' ');
                }
            }
            continue;
        }

        if (reader.isEndElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("t")) {
                inText = false;
            } else if (name == QStringLiteral("tc")) {
                rowCells.append(cellText);
                cellText.clear();
            } else if (name == QStringLiteral("tr")) {
                emitSegment(rowCells.join(QStringLiteral(" | ")));
                rowCells.clear();
            } else if (name == QStringLiteral("tbl")) {
                inTable = false;
            } else if (name == QStringLiteral("p") && !inTable) {
                emitSegment(paragraph);
                paragraph.clear();
            }
        }
    }

    if (reader.hasError()) {
        setError(error, QStringLiteral("docx 正文 XML 解析失败：%1").arg(reader.errorString()));
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
