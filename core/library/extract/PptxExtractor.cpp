#include "core/library/extract/PptxExtractor.h"

#include "core/library/extract/OfficeXmlUtils.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>

namespace {

/// 幻灯片条目：`ppt/slides/slide12.xml`，捕获组为幻灯片编号
const QRegularExpression &slideEntryPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^ppt/slides/slide(\\d+)\\.xml$"));
    return pattern;
}

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

/// 幻灯片编号（文件名里的数字）；无法识别返回 -1
int slideNumberOf(const QString &entryName)
{
    const QRegularExpressionMatch match = slideEntryPattern().match(entryName);
    if (!match.hasMatch()) {
        return -1;
    }
    return match.captured(1).toInt();
}

/// 单张幻灯片：按 `<a:p>` 段落收集 `<a:t>` 文本
QString slideText(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    QStringList paragraphs;
    QString paragraph;
    bool inText = false;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isCharacters()) {
            if (inText) {
                paragraph += reader.text().toString();
            }
            continue;
        }

        if (reader.isStartElement()) {
            if (officeElementName(reader) == QStringLiteral("t")) {
                inText = true;
            }
            continue;
        }

        if (reader.isEndElement()) {
            const QString name = officeElementName(reader);
            if (name == QStringLiteral("t")) {
                inText = false;
            } else if (name == QStringLiteral("p")) {
                const QString text = collapseXmlWhitespace(paragraph);
                if (!text.isEmpty()) {
                    paragraphs.append(text);
                }
                paragraph.clear();
            }
        }
    }

    if (reader.hasError()) {
        return QString();
    }
    return paragraphs.join(QLatin1Char('\n'));
}

}  // namespace

QString PptxExtractor::id() const
{
    return QStringLiteral("pptx");
}

bool PptxExtractor::supports(LibraryKind kind, const QString &extension) const
{
    return kind == LibraryKind::Text && extension == QStringLiteral("pptx");
}

bool PptxExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }

    OfficeZipArchive archive;
    if (!archive.open(path, error)) {
        return false;
    }

    QList<QPair<int, QString>> slides;  // 幻灯片编号 → 条目名
    const QStringList names = archive.names();
    for (const QString &name : names) {
        const int number = slideNumberOf(name);
        if (number > 0) {
            slides.append({ number, name });
        }
    }
    if (slides.isEmpty()) {
        setError(error, QStringLiteral("不是有效的 pptx（未找到幻灯片）"));
        return false;
    }
    std::sort(slides.begin(), slides.end(),
              [](const QPair<int, QString> &left, const QPair<int, QString> &right) {
                  return left.first < right.first;
              });

    QList<ExtractedSegment> segments;
    int ordinal = 0;
    for (const QPair<int, QString> &slide : slides) {
        ++ordinal;
        QByteArray xml;
        if (!archive.read(slide.second, &xml, nullptr)) {
            continue;  // 单张损坏不中断整册
        }
        const QString text = slideText(xml);
        if (text.isEmpty()) {
            continue;  // 无文本的幻灯片不产生片段
        }
        ExtractedSegment segment;
        segment.text = text;
        segment.locKind = LocatorKind::Slide;
        segment.locPage = ordinal;
        segment.locLabel = QStringLiteral("第 %1 张幻灯片").arg(ordinal);
        segments.append(segment);
    }

    if (segments.isEmpty()) {
        setError(error, QStringLiteral("pptx 的幻灯片没有可抽取的文本"));
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
