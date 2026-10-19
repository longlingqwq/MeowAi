#include "providers/ProviderUtils.h"

#include <QFile>
#include <QFileInfo>

namespace ProviderUtils {

QString defaultBaseUrl(AiProvider::Protocol protocol)
{
    switch (protocol) {
    case AiProvider::OpenAI:
        return QStringLiteral("https://api.openai.com/v1");
    case AiProvider::Anthropic:
        return QStringLiteral("https://api.anthropic.com/v1");
    }
    return QString();
}

QString resolveBaseUrl(const ProviderConfig &config, AiProvider::Protocol protocol)
{
    QString base = config.baseUrl.trimmed();
    if (base.isEmpty())
        base = defaultBaseUrl(protocol);
    while (base.endsWith('/'))
        base.chop(1);
    return base;
}

QString guessMimeType(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QStringLiteral("webp"))
        return QStringLiteral("image/webp");
    if (suffix == QStringLiteral("gif"))
        return QStringLiteral("image/gif");
    if (suffix == QStringLiteral("bmp"))
        return QStringLiteral("image/bmp");
    return QStringLiteral("image/png");
}

bool readImage(const ChatImage &image, QByteArray *bytes, QString *mimeType, QString *error)
{
    QByteArray data = image.bytes;
    if (data.isEmpty() && !image.path.isEmpty()) {
        QFile file(image.path);
        if (!file.exists()) {
            if (error)
                *error = QStringLiteral("图片文件不存在");
            return false;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("图片文件无法读取");
            return false;
        }
        data = file.readAll();
    }

    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("图片内容为空");
        return false;
    }

    QString mime = image.mimeType.trimmed();
    if (mime.isEmpty())
        mime = guessMimeType(image.path);

    if (bytes)
        *bytes = data;
    if (mimeType)
        *mimeType = mime;
    return true;
}

QString maskSecret(const QString &secret)
{
    if (secret.isEmpty())
        return QStringLiteral("<empty>");
    if (secret.size() <= 4)
        return QStringLiteral("****");
    return secret.left(4) + QStringLiteral("****");
}

}  // namespace ProviderUtils
