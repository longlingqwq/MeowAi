#include "core/library/extract/PlainTextExtractor.h"

#include <QRegularExpression>
#include <QSet>

namespace {

/// 空行（允许行内只有空白）即为段落边界
const QRegularExpression &blankLineSeparator()
{
    static const QRegularExpression pattern(QStringLiteral("\\n[ \\t]*\\n"));
    return pattern;
}

/// Markdown 标题行：`## 施工规范`（CommonMark 要求 `#` 后有空白）
const QRegularExpression &markdownHeading()
{
    static const QRegularExpression pattern(QStringLiteral("^(#{1,6})\\s+(.*)$"));
    return pattern;
}

}  // namespace

QString PlainTextExtractor::id() const
{
    return QStringLiteral("plain");
}

bool PlainTextExtractor::supports(LibraryKind kind, const QString &extension) const
{
    if (kind != LibraryKind::Text) {
        return false;
    }
    static const QSet<QString> extensions = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("log"),
    };
    return extensions.contains(extension);
}

bool PlainTextExtractor::extract(const QString &path, ExtractionResult *result,
                                 QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }

    QString text;
    if (!readLibraryTextFile(path, &text, error)) {
        return false;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool markdown = suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown");

    QList<ExtractedSegment> segments;
    int ordinal = 0;

    const auto appendSegment = [&segments, &ordinal](const QString &body, int headingLevel) {
        if (body.isEmpty()) {
            return;
        }
        ++ordinal;
        ExtractedSegment segment;
        segment.text = body;
        segment.locKind = LocatorKind::Para;
        segment.locLabel = QStringLiteral("第 %1 段").arg(ordinal);
        segment.headingLevel = headingLevel;
        segments.append(segment);
    };

    if (!markdown) {
        // 纯文本：按空行切成语义块；块内再按长度切分与重叠由 M4 的 ChunkSplitter 负责
        const QStringList blocks = text.split(blankLineSeparator(), Qt::SkipEmptyParts);
        for (const QString &block : blocks) {
            appendSegment(block.trimmed(), 0);
        }
    } else {
        // Markdown：按行扫描，`#` 标题单独成段并带层级，正文按空行聚合
        QString buffer;
        bool inFence = false;

        const auto flush = [&buffer, &appendSegment]() {
            const QString body = buffer.trimmed();
            buffer.clear();
            appendSegment(body, 0);
        };

        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("```"))) {
                // 代码围栏内的 `#` 不是标题
                inFence = !inFence;
                buffer += line + QLatin1Char('\n');
                continue;
            }

            if (!inFence) {
                const QRegularExpressionMatch match = markdownHeading().match(trimmed);
                if (match.hasMatch()) {
                    flush();
                    appendSegment(match.captured(2).trimmed(), match.captured(1).size());
                    continue;
                }
                if (trimmed.isEmpty()) {
                    flush();
                    continue;
                }
            }
            buffer += line + QLatin1Char('\n');
        }
        flush();
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
