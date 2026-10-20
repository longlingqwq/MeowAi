#include "core/web/HttpWebSearchProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QUrl>

namespace {

/// 响应体累积上限（512KB）：防超大页面把内存吃满，超出即截断
constexpr int kMaxBodyBytes = 512 * 1024;

/// 从对象里按候选键取第一个非空字符串
QString pickString(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty())
                return text;
        }
    }
    return QString();
}

/**
 * 在对象中递归寻找第一个「结果数组」，兼容常见搜索端点返回结构：
 * `{results:[…]}` / `{data:[…]}` / `{items:[…]}` / `{hits:[…]}` / `{web:{results:[…]}}` 等。
 */
QJsonArray findResultArray(const QJsonObject &object, int depth = 0)
{
    if (depth > 3)
        return QJsonArray();

    static const QStringList keys{QStringLiteral("results"), QStringLiteral("data"),
                                  QStringLiteral("items"), QStringLiteral("hits"),
                                  QStringLiteral("entries"), QStringLiteral("organic"),
                                  QStringLiteral("web")};
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isArray())
            return value.toArray();
        if (value.isObject()) {
            const QJsonArray nested = findResultArray(value.toObject(), depth + 1);
            if (!nested.isEmpty())
                return nested;
        }
    }
    return QJsonArray();
}

/// 抽取 <title>…</title>，失败返回空串
QString htmlTitle(const QString &html)
{
    const QRegularExpression pattern(QStringLiteral("<title[^>]*>(.*?)</title>"),
                                     QRegularExpression::DotMatchesEverythingOption
                                         | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(html);
    if (!match.hasMatch())
        return QString();
    return match.captured(1).trimmed();
}

/// 去标签 + 去脚本样式 + 解码常见实体 + 折叠空白，并裁剪到长度上限
QString htmlToText(const QString &html, int maxChars)
{
    QString text = html;
    text.remove(QRegularExpression(QStringLiteral("<script[^>]*>.*?</script>"),
                                   QRegularExpression::DotMatchesEverythingOption
                                       | QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"),
                                   QRegularExpression::DotMatchesEverythingOption
                                       | QRegularExpression::CaseInsensitiveOption));
    // 块级标签换成换行，避免正文被压成一整行
    text.replace(QRegularExpression(QStringLiteral("<(br|/p|/div|/li|/h[1-6]|/tr)[^>]*>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));

    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&mdash;"), QStringLiteral("—"));

    text.replace(QRegularExpression(QStringLiteral("[ \\t\\r\\f\\v]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n\\s*\\n+")), QStringLiteral("\n"));
    text = text.trimmed();

    if (maxChars > 0 && text.size() > maxChars)
        text = text.left(maxChars) + QStringLiteral(" …（正文已截断）");
    return text;
}

}  // namespace

HttpWebSearchProvider::HttpWebSearchProvider(NetworkTransport *transport, QObject *parent)
    : WebSearchProvider(parent)
{
    m_transport = transport ? transport : new QNetworkTransport(this);

    connect(m_transport, &NetworkTransport::chunkReceived, this,
            [this](int handle, const QByteArray &chunk) { handleChunk(handle, chunk); });
    connect(m_transport, &NetworkTransport::finished, this,
            [this](int handle) { handleFinished(handle); });
    connect(m_transport, &NetworkTransport::failed, this,
            [this](int handle, int status, NetworkTransport::Failure failure, const QString &text) {
                handleFailed(handle, status, failure, text);
            });
}

QString HttpWebSearchProvider::backendId() const
{
    return QStringLiteral("http");
}

void HttpWebSearchProvider::configure(const WebSearchConfig &config)
{
    m_config = config;
}

bool HttpWebSearchProvider::isDeepSeek() const
{
    return m_config.protocol == WebSearchConfig::Protocol::DeepSeek;
}

QString HttpWebSearchProvider::buildSearchUrl(const QString &query) const
{
    QString url = m_config.endpoint.trimmed();
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query));
    if (url.contains(QStringLiteral("{query}")))
        url.replace(QStringLiteral("{query}"), encoded);
    else
        url += (url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?'))
            + QStringLiteral("q=") + encoded;
    return url;
}

NetworkTransport::Request HttpWebSearchProvider::requestFor(const QString &url) const
{
    NetworkTransport::Request request;
    request.url = url;
    request.method = QStringLiteral("GET");
    request.timeoutMs = m_config.timeoutMs;
    request.headers.append({QStringLiteral("Accept"),
                            QStringLiteral("application/json, text/html;q=0.9, */*;q=0.8")});
    request.headers.append({QStringLiteral("User-Agent"), QStringLiteral("MeowAi/0.2.4")});
    // 密钥只随请求头走（**不进 URL**，避免被日志 / 历史记录捕获）
    if (!m_config.apiKey.trimmed().isEmpty()) {
        request.headers.append({QStringLiteral("Authorization"),
                                QStringLiteral("Bearer ") + m_config.apiKey.trimmed()});
    }
    return request;
}

NetworkTransport::Request HttpWebSearchProvider::jsonRequest(const QString &url,
                                                             const QByteArray &body) const
{
    NetworkTransport::Request request;
    request.url = url;
    request.method = QStringLiteral("POST");
    request.timeoutMs = m_config.timeoutMs;
    request.body = body;
    request.headers.append({QStringLiteral("Accept"), QStringLiteral("application/json")});
    request.headers.append({QStringLiteral("Content-Type"), QStringLiteral("application/json")});
    request.headers.append({QStringLiteral("User-Agent"), QStringLiteral("MeowAi/0.2.4")});
    // 内置协议复用厂商 Key：仍只随请求头走，不进 URL / 不进日志
    if (!m_config.apiKey.trimmed().isEmpty()) {
        request.headers.append({QStringLiteral("Authorization"),
                                QStringLiteral("Bearer ") + m_config.apiKey.trimmed()});
    }
    return request;
}

void HttpWebSearchProvider::search(const QString &query, int topK)
{
    if (m_handle != 0)
        cancel();

    m_mode = Mode::Search;
    m_topK = topK > 0 ? topK : 5;
    m_body.clear();
    m_pageUrl.clear();

    if (m_config.endpoint.trimmed().isEmpty()) {
        failCurrent(QStringLiteral("未配置搜索端点"));
        return;
    }
    const QString keyword = query.trimmed();
    if (keyword.isEmpty()) {
        failCurrent(QStringLiteral("检索关键词为空"));
        return;
    }

    if (isDeepSeek()) {
        // DeepSeek 内置检索：POST JSON {"query","count"}
        QJsonObject payload;
        payload.insert(QStringLiteral("query"), keyword);
        payload.insert(QStringLiteral("count"), m_topK);
        m_handle = m_transport->post(
            jsonRequest(m_config.endpoint.trimmed(), QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    } else {
        m_handle = m_transport->get(requestFor(buildSearchUrl(keyword)));
    }
    if (m_handle == 0)
        failCurrent(QStringLiteral("无法发起检索请求"));
}

void HttpWebSearchProvider::fetch(const QString &url)
{
    if (m_handle != 0)
        cancel();

    m_mode = Mode::Fetch;
    m_body.clear();
    m_pageUrl = url.trimmed();

    if (!m_pageUrl.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
        failCurrent(QStringLiteral("无效的抓取地址"));
        return;
    }

    if (isDeepSeek()) {
        // DeepSeek 内置抓取：POST JSON {"url"}
        if (m_config.fetchEndpoint.trimmed().isEmpty()) {
            failCurrent(QStringLiteral("未配置网页抓取端点"));
            return;
        }
        QJsonObject payload;
        payload.insert(QStringLiteral("url"), m_pageUrl);
        m_handle = m_transport->post(jsonRequest(m_config.fetchEndpoint.trimmed(),
                                                 QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    } else {
        m_handle = m_transport->get(requestFor(m_pageUrl));
    }
    if (m_handle == 0)
        failCurrent(QStringLiteral("无法发起抓取请求"));
}

void HttpWebSearchProvider::cancel()
{
    if (m_handle == 0)
        return;
    const int handle = m_handle;
    m_handle = 0;
    m_mode = Mode::None;
    m_transport->abort(handle);
}

void HttpWebSearchProvider::handleChunk(int handle, const QByteArray &chunk)
{
    if (handle != m_handle || m_handle == 0)
        return;
    if (m_body.size() >= kMaxBodyBytes)
        return;
    m_body.append(chunk);
    if (m_body.size() > kMaxBodyBytes)
        m_body.truncate(kMaxBodyBytes);
}

void HttpWebSearchProvider::handleFinished(int handle)
{
    if (handle != m_handle)
        return;
    m_handle = 0;
    const Mode mode = m_mode;
    m_mode = Mode::None;

    if (mode == Mode::Search) {
        const QList<WebSearchResult> results = parseResults();
        if (results.isEmpty()) {
            emit searchFinished(QList<WebSearchResult>(),
                                QStringLiteral("未能从检索端点解析出结果（请确认端点返回 JSON，"
                                               "且含 results / data / items 数组）"));
        } else {
            emit searchFinished(results, QString());
        }
        return;
    }

    if (mode == Mode::Fetch) {
        const WebFetchResult result = parseFetch();
        if (result.text.isEmpty())
            emit fetchFinished(WebFetchResult(), QStringLiteral("未能提取到网页正文"));
        else
            emit fetchFinished(result, QString());
    }
}

void HttpWebSearchProvider::handleFailed(int handle, int httpStatus,
                                         NetworkTransport::Failure failure,
                                         const QString &errorText)
{
    if (handle != m_handle)
        return;
    m_handle = 0;
    const Mode mode = m_mode;
    m_mode = Mode::None;
    if (mode == Mode::None)
        return;

    QString message;
    switch (failure) {
    case NetworkTransport::Failure::Timeout:
        message = QStringLiteral("检索请求超时");
        break;
    case NetworkTransport::Failure::NetworkUnavailable:
        message = QStringLiteral("网络不可用");
        break;
    case NetworkTransport::Failure::Cancelled:
        message = QStringLiteral("检索已取消");
        break;
    case NetworkTransport::Failure::Other:
        if (httpStatus == 401 || httpStatus == 403)
            message = QStringLiteral("搜索密钥无效或无权限");
        else if (httpStatus == 429)
            message = QStringLiteral("检索请求被限流");
        else if (httpStatus >= 500)
            message = QStringLiteral("检索服务端错误");
        else if (httpStatus > 0)
            message = QStringLiteral("HTTP 错误 %1").arg(httpStatus);
        else
            message = errorText.isEmpty() ? QStringLiteral("检索请求失败") : errorText;
        break;
    }

    if (mode == Mode::Search)
        emit searchFinished(QList<WebSearchResult>(), message);
    else
        emit fetchFinished(WebFetchResult(), message);
}

void HttpWebSearchProvider::failCurrent(const QString &message)
{
    const Mode mode = m_mode;
    m_mode = Mode::None;
    m_handle = 0;
    if (mode == Mode::Fetch)
        emit fetchFinished(WebFetchResult(), message);
    else
        emit searchFinished(QList<WebSearchResult>(), message);
}

QList<WebSearchResult> HttpWebSearchProvider::parseResults() const
{
    QList<WebSearchResult> results;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(m_body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return results;

    QJsonArray entries;
    if (document.isArray())
        entries = document.array();
    else if (document.isObject())
        entries = findResultArray(document.object());

    for (const QJsonValue &entry : entries) {
        const QJsonObject object = entry.toObject();
        WebSearchResult result;
        result.title = pickString(object, {QStringLiteral("title"), QStringLiteral("name"),
                                           QStringLiteral("heading")});
        result.url = pickString(object, {QStringLiteral("url"), QStringLiteral("link"),
                                         QStringLiteral("href"), QStringLiteral("uri"),
                                         QStringLiteral("permalink")});
        result.snippet = pickString(object, {QStringLiteral("snippet"),
                                             QStringLiteral("description"),
                                             QStringLiteral("summary"),
                                             QStringLiteral("content"), QStringLiteral("text"),
                                             QStringLiteral("body")});
        if (result.url.isEmpty())
            continue;
        if (result.title.isEmpty())
            result.title = result.url;
        results.append(result);
        if (results.size() >= m_topK)
            break;
    }
    return results;
}

WebFetchResult HttpWebSearchProvider::parseFetch() const
{
    WebFetchResult result;
    result.url = m_pageUrl;

    // DeepSeek 内置抓取：响应为 JSON {"title","content"(Markdown)}
    if (isDeepSeek()) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(m_body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return result;
        const QJsonObject object = document.object();
        result.title = object.value(QStringLiteral("title")).toString().trimmed();
        QString content = object.value(QStringLiteral("content")).toString().trimmed();
        if (m_config.maxFetchChars > 0 && content.size() > m_config.maxFetchChars)
            content = content.left(m_config.maxFetchChars) + QStringLiteral(" …（正文已截断）");
        result.text = content;
        if (result.title.isEmpty())
            result.title = m_pageUrl;
        return result;
    }

    const QString html = QString::fromUtf8(m_body);
    result.title = htmlTitle(html);
    result.text = htmlToText(html, m_config.maxFetchChars);
    if (result.title.isEmpty())
        result.title = m_pageUrl;
    return result;
}
