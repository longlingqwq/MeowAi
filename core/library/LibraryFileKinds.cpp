#include "core/library/LibraryFileKinds.h"

#include <QFileInfo>
#include <QSet>

namespace {

const QSet<QString> &textExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("txt"), QStringLiteral("md"),    QStringLiteral("markdown"),
        QStringLiteral("csv"), QStringLiteral("tsv"),   QStringLiteral("json"),
        QStringLiteral("log"), QStringLiteral("pdf"),   QStringLiteral("docx"),
        QStringLiteral("xlsx"), QStringLiteral("pptx"),
    };
    return extensions;
}

const QSet<QString> &imageExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("gif"),  QStringLiteral("bmp"),
    };
    return extensions;
}

/// 本次仅登记类别、**不做解析与索引**（视频留待后续阶段）
const QSet<QString> &videoExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("m4v"),
    };
    return extensions;
}

}  // namespace

QString libraryExtension(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

LibraryKind libraryKindForPath(const QString &path)
{
    const QString extension = libraryExtension(path);
    if (extension.isEmpty()) {
        return LibraryKind::Unknown;
    }
    if (textExtensions().contains(extension)) {
        return LibraryKind::Text;
    }
    if (imageExtensions().contains(extension)) {
        return LibraryKind::Image;
    }
    if (videoExtensions().contains(extension)) {
        return LibraryKind::Video;
    }
    return LibraryKind::Unknown;
}

QString libraryKindName(LibraryKind kind)
{
    switch (kind) {
    case LibraryKind::Text:
        return QStringLiteral("text");
    case LibraryKind::Image:
        return QStringLiteral("image");
    case LibraryKind::Video:
        return QStringLiteral("video");
    case LibraryKind::Unknown:
        break;
    }
    return QString();
}

bool libraryKindIsIndexable(LibraryKind kind)
{
    return kind == LibraryKind::Text || kind == LibraryKind::Image;
}
