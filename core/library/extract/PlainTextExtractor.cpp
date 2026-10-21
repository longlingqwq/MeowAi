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

    // 按空行切成语义块；块内再按长度切分与重叠由 M4 的 ChunkSplitter 负责
    const QStringList blocks = text.split(blankLineSeparator(), Qt::SkipEmptyParts);
    int ordinal = 0;
    for (const QString &block : blocks) {
        const QString body = block.trimmed();
        if (body.isEmpty()) {
            continue;
        }
        ++ordinal;
        ExtractedSegment segment;
        segment.text = body;
        segment.locKind = LocatorKind::Para;
        segment.locLabel = QStringLiteral("第 %1 段").arg(ordinal);
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
