#include "providers/ProviderUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

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

QString endpointUrl(const ProviderConfig &config, AiProvider::Protocol protocol, const QString &path)
{
    const QString url = resolveBaseUrl(config, protocol) + path;
    if (config.authStyle.trimmed() != QStringLiteral("query-key") || config.apiKey.isEmpty())
        return url;

    QUrl parsed(url);
    QUrlQuery query(parsed);
    query.addQueryItem(QStringLiteral("key"), config.apiKey);
    parsed.setQuery(query);
    return parsed.toString();
}

QList<QPair<QString, QString>> authHeaders(const ProviderConfig &config)
{
    const QString style = config.authStyle.trimmed();
    QList<QPair<QString, QString>> headers;
    if (config.apiKey.isEmpty())
        return headers;

    if (style == QStringLiteral("x-api-key")) {
        headers.append({QStringLiteral("x-api-key"), config.apiKey});
    } else if (style == QStringLiteral("query-key")) {
        // Key 走 URL 查询参数，见 endpointUrl()；此处不生成鉴权头
    } else {
        headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer ") + config.apiKey});
    }
    return headers;
}

QList<QPair<QString, QString>> requestHeaders(const ProviderConfig &config, bool stream)
{
    QList<QPair<QString, QString>> headers;
    headers.append({QStringLiteral("Content-Type"), QStringLiteral("application/json")});
    headers.append({QStringLiteral("Accept"), stream ? QStringLiteral("text/event-stream")
                                                    : QStringLiteral("application/json")});
    headers.append(authHeaders(config));

    // 厂商额外头只做补充，不覆盖已有键
    for (auto it = config.extraHeaders.constBegin(); it != config.extraHeaders.constEnd(); ++it) {
        if (!it.value().isString() || it.key().isEmpty())
            continue;
        bool exists = false;
        for (const auto &header : headers) {
            if (header.first.compare(it.key(), Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists)
            headers.append({it.key(), it.value().toString()});
    }
    return headers;
}

void mergeNativeParams(QJsonObject *body, const QJsonObject &nativeParams)
{
    if (!body || nativeParams.isEmpty())
        return;

    static const QStringList reserved{QStringLiteral("model"), QStringLiteral("messages"),
                                      QStringLiteral("stream"), QStringLiteral("system")};

    for (auto it = nativeParams.constBegin(); it != nativeParams.constEnd(); ++it) {
        if (reserved.contains(it.key()))
            continue;
        body->insert(it.key(), it.value());
    }
}

CredentialStatus credentialStatusForHttp(int httpStatus)
{
    if (httpStatus >= 200 && httpStatus < 300)
        return CredentialStatus::Valid;
    if (httpStatus == 401 || httpStatus == 403)
        return CredentialStatus::Invalid;
    return CredentialStatus::Unverifiable;
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
