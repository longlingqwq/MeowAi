#include "core/web/WebSearchTool.h"

#include "core/web/WebSearchService.h"

#include <QJsonArray>

WebSearchTool::WebSearchTool(WebSearchService *service, QObject *parent)
    : QObject(parent)
    , m_service(service)
{
    if (m_service) {
        connect(m_service, &WebSearchService::finished, this, &WebSearchTool::onServiceFinished);
    }
}

QString WebSearchTool::toolName()
{
    return QStringLiteral("web_search");
}

QJsonObject WebSearchTool::descriptor()
{
    QJsonObject queryProperty;
    queryProperty.insert(QStringLiteral("type"), QStringLiteral("string"));
    queryProperty.insert(QStringLiteral("description"),
                         QStringLiteral("要联网检索的关键词或问题，尽量精炼、包含关键实体"));

    QJsonObject properties;
    properties.insert(QStringLiteral("query"), queryProperty);

    QJsonArray required;
    required.append(QStringLiteral("query"));

    QJsonObject parameters;
    parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
    parameters.insert(QStringLiteral("properties"), properties);
    parameters.insert(QStringLiteral("required"), required);

    QJsonObject function;
    function.insert(QStringLiteral("name"), toolName());
    function.insert(QStringLiteral("description"),
                    QStringLiteral("联网搜索：当问题涉及最新信息、实时数据、你不确定或知识截止之后的事实时调用。"
                                   "返回若干网页的标题、链接与摘要（必要时含正文）。"));
    function.insert(QStringLiteral("parameters"), parameters);

    QJsonObject tool;
    tool.insert(QStringLiteral("type"), QStringLiteral("function"));
    tool.insert(QStringLiteral("function"), function);
    return tool;
}

void WebSearchTool::configure(const WebSearchConfig &config)
{
    // 服务为空时静默忽略：execute() 会明确回传错误
    if (m_service)
        m_service->configure(config);
}

void WebSearchTool::clearConfig()
{
    if (m_service)
        m_service->clearRuntimeConfig();
}

bool WebSearchTool::configured() const
{
    return m_service && m_service->configured();
}

QString WebSearchTool::statusMessage() const
{
    if (!m_service)
        return QStringLiteral("联网搜索模块未装配");
    return m_service->statusMessage();
}

bool WebSearchTool::busy() const
{
    return m_running;
}

void WebSearchTool::execute(const QString &query)
{
    if (!m_service) {
        emit finished(QString(), QList<WebSearchResult>(), QStringLiteral("联网搜索模块未装配"));
        return;
    }
    if (query.trimmed().isEmpty()) {
        emit finished(QString(), QList<WebSearchResult>(), QStringLiteral("缺少检索关键词"));
        return;
    }
    m_running = true;
    m_service->run(query.trimmed());
}

void WebSearchTool::cancel()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_service)
        m_service->cancel();
}

void WebSearchTool::onServiceFinished(const QString &context, const QList<WebSearchResult> &sources,
                                      const QString &error)
{
    // 只接受本类发起的那次检索（WebSearchService 也可能被其它路径使用）
    if (!m_running)
        return;
    m_running = false;
    emit finished(context, sources, error);
}
