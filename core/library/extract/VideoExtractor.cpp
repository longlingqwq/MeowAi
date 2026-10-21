#include "core/library/extract/VideoExtractor.h"

QString VideoExtractor::id() const
{
    return QStringLiteral("video");
}

bool VideoExtractor::supports(LibraryKind kind, const QString &extension) const
{
    Q_UNUSED(extension);
    return kind == LibraryKind::Video;
}

bool VideoExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }
    Q_UNUSED(path);
    if (error) {
        *error = QStringLiteral("视频解析本次未实现（仅预留注册点）");
    }
    return false;
}
