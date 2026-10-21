#include "core/library/extract/ImageCaptionExtractor.h"

#include "core/library/extract/ImageTranscoder.h"

ImageCaptionExtractor::ImageCaptionExtractor(int maxEdge)
    : m_maxEdge(maxEdge > 0 ? maxEdge : ImageTranscoder::kDefaultMaxEdge)
{
}

QString ImageCaptionExtractor::id() const
{
    return QStringLiteral("image");
}

bool ImageCaptionExtractor::supports(LibraryKind kind, const QString &extension) const
{
    // 白名单即 LibraryFileKinds 的图片扩展名；Unknown/Video 一律不认领
    Q_UNUSED(extension);
    return kind == LibraryKind::Image;
}

bool ImageCaptionExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }
    if (error) {
        error->clear();
    }

    ImageCaptionTask task;
    if (!ImageTranscoder::transcode(path, m_maxEdge, &task, error)) {
        return false;
    }

    if (result) {
        result->ok = true;
        result->captionTasks.append(task);
    }
    return true;
}

ExtractedSegment ImageCaptionExtractor::segmentFromCaption(int imageOrdinal, const QString &caption)
{
    const int ordinal = imageOrdinal > 0 ? imageOrdinal : 1;

    ExtractedSegment segment;
    segment.text = caption.trimmed();
    segment.locKind = LocatorKind::Image;
    segment.locLabel = QStringLiteral("图片 #%1").arg(ordinal);
    return segment;
}
