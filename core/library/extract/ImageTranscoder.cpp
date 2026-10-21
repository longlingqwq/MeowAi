#include "core/library/extract/ImageTranscoder.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSize>

namespace {

/// 长边压到 maxEdge：**只缩不放**（放大既无益又多耗 token）
QImage shrinkToMaxEdge(const QImage &image, int maxEdge)
{
    const int longEdge = qMax(image.width(), image.height());
    if (longEdge <= maxEdge) {
        return image;
    }
    return image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/// 编码为可外发字节：优先 PNG（图文/截图无损，文字不糊），失败时回落 JPEG
bool encodeForUpload(const QImage &image, QByteArray *bytes, QString *mimeType)
{
    const auto encode = [&image](const char *format, QByteArray *out) {
        QByteArray buffer;
        QBuffer device(&buffer);
        if (!device.open(QIODevice::WriteOnly)) {
            return false;
        }
        const bool saved = image.save(&device, format);
        device.close();
        if (!saved || buffer.isEmpty()) {
            return false;
        }
        *out = buffer;
        return true;
    };

    if (encode("PNG", bytes)) {
        *mimeType = QStringLiteral("image/png");
        return true;
    }
    if (encode("JPEG", bytes)) {
        *mimeType = QStringLiteral("image/jpeg");
        return true;
    }
    return false;
}

}  // namespace

bool ImageTranscoder::transcode(const QString &path, int maxEdge, ImageCaptionTask *task,
                                QString *error)
{
    if (task) {
        *task = ImageCaptionTask();
    }
    if (error) {
        error->clear();
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error) {
            *error = QStringLiteral("图片不存在或不是普通文件：%1").arg(path);
        }
        return false;
    }
    if (info.size() > kMaxExtractBytes) {
        if (error) {
            *error = QStringLiteral("图片文件过大（%1 MB），超出单张上限 %2 MB")
                         .arg(info.size() / (1024 * 1024))
                         .arg(kMaxExtractBytes / (1024 * 1024));
        }
        return false;
    }

    const int limit = maxEdge > 0 ? maxEdge : kDefaultMaxEdge;

    QImageReader reader(path);
    reader.setAutoTransform(true);  // 按 EXIF 方向校正，避免「躺倒」的图片影响视觉模型判断
    if (!reader.canRead()) {
        if (error) {
            // 列出运行库当前可读格式：webp 等格式依赖 Qt 的 imageformats 插件，缺失时据此自查
            *error = QStringLiteral("无法解码该图片（格式不受支持或文件已损坏）；"
                                    "当前 Qt 运行库可读格式：%1")
                         .arg(QString::fromLatin1(QImageReader::supportedImageFormats().join(", ")));
        }
        return false;
    }

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()
        && qint64(sourceSize.width()) * qint64(sourceSize.height()) > kMaxSourcePixels) {
        if (error) {
            *error = QStringLiteral("图片像素过大（%1 × %2），超出单张上限；请先自行压缩后再导入")
                         .arg(sourceSize.width())
                         .arg(sourceSize.height());
        }
        return false;
    }

    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        if (error) {
            *error = QStringLiteral("图片解码失败：%1").arg(reader.errorString());
        }
        return false;
    }

    const QImage shrunk = shrinkToMaxEdge(decoded, limit);

    QByteArray bytes;
    QString mimeType;
    if (!encodeForUpload(shrunk, &bytes, &mimeType)) {
        if (error) {
            *error = QStringLiteral("图片转码失败：无法编码为 PNG / JPEG");
        }
        return false;
    }

    if (task) {
        task->bytes = bytes;
        task->mimeType = mimeType;
        task->width = shrunk.width();
        task->height = shrunk.height();
        task->sourcePath = path;
    }
    return true;
}
