#include "core/web/WebSearchService.h"

#include "storage/SecureStore.h"
#include "storage/WebConfigStore.h"

#include <QStringList>

namespace {

/// 一次检索取回的候选条数上限
constexpr int kTopK = 5;
/// 对前若干条结果再抓正文（抓取比检索慢，条数刻意少）
constexpr int kFetchTopN = 2;

}  // namespace

WebSearchService::WebSearchService(WebSearchProvider *provider, SecureStore *secureStore,
                                   WebConfigStore *configStore, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
    , m_secureStore(secureStore)
    , m_configStore(configStore)
{
    if (m_provider) {
        connect(m_provider, &WebSearchProvider::searchFinished, this,
                &WebSearchService::onSearchFinished);
        connect(m_provider, &WebSearchProvider::fetchFinished, this,
                &WebSearchService::onFetchFinished);
    }
}

bool WebSearchService::configured() const
{
    return statusMessage().isEmpty();
}

bool WebSearchService::busy() const
{
    return m_running;
}

QString WebSearchService::statusMessage() const
{
    if (!m_provider)
        return QStringLiteral("联网搜索模块未装配");

    // ① 运行时方案（厂商内置搜索 / 由上层解析后注入）：以它为唯一判据
    if (m_hasRuntimeConfig) {
        if (m_runtimeConfig.endpoint.trimmed().isEmpty())
            return QStringLiteral("联网搜索未配置：缺少搜索端点");
        if (m_runtimeConfig.apiKey.trimmed().isEmpty())
            return QStringLiteral("联网搜索未配置：缺少搜索密钥");
        return QString();
    }

    // ② 默认路径：端点来自 WebConfigStore、密钥来自 SecureStore
    if (!m_configStore)
        return QStringLiteral("联网搜索模块未装配");

    const WebConfigData data = m_configStore->load();
    if (data.endpoint.trimmed().isEmpty())
        return QStringLiteral("联网搜索未配置：请先在「联网」里填写搜索端点");

    if (!m_secureStore || !m_secureStore->isAvailable())
        return QStringLiteral("联网搜索不可用：本平台不支持安全存储，无法保存搜索密钥");
    if (!m_secureStore->has(SecureStore::webSearchApiKeyId()))
        return QStringLiteral("联网搜索未配置：请先保存搜索密钥");

    return QString();
}

void WebSearchService::configure(const WebSearchConfig &config)
{
    m_runtimeConfig = config;
    m_hasRuntimeConfig = true;
}

void WebSearchService::clearRuntimeConfig()
{
    m_runtimeConfig = WebSearchConfig{};
    m_hasRuntimeConfig = false;
}

void WebSearchService::run(const QString &query)
{
    cancel();

    const QString problem = statusMessage();
    if (!problem.isEmpty()) {
        finishWithError(problem);
        return;
    }

    WebSearchConfig config;
    if (m_hasRuntimeConfig) {
        config = m_runtimeConfig;
    } else {
        QString apiKey;
        QString keyError;
        if (!m_secureStore
            || !m_secureStore->load(SecureStore::webSearchApiKeyId(), &apiKey, &keyError)
            || apiKey.isEmpty()) {
            finishWithError(QStringLiteral("联网搜索未配置：请先保存搜索密钥"));
            return;
        }
        const WebConfigData data = m_configStore->load();
        config.endpoint = data.endpoint;
        config.apiKey = apiKey;
    }
    // 兜底默认值：调用方未显式设置时仍给出合理超时 / 裁剪上限
    if (config.timeoutMs <= 0)
        config.timeoutMs = 15000;
    if (config.maxFetchChars <= 0)
        config.maxFetchChars = 6000;
    m_provider->configure(config);

    m_query = query;
    m_results.clear();
    m_fetched.clear();
    m_fetchIndex = 0;
    m_running = true;
    m_provider->search(query, kTopK);
}

void WebSearchService::cancel()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_provider)
        m_provider->cancel();
}

void WebSearchService::onSearchFinished(const QList<WebSearchResult> &results,
                                        const QString &error)
{
    if (!m_running)
        return;
    if (!error.isEmpty()) {
        finishWithError(error);
        return;
    }
    if (results.isEmpty()) {
        finishWithError(QStringLiteral("未检索到相关结果"));
        return;
    }

    m_results = results;
    m_fetchIndex = 0;
    startNextFetch();
}

void WebSearchService::onFetchFinished(const WebFetchResult &result, const QString &error)
{
    if (!m_running)
        return;
    // 单页抓取失败不影响整体：跳过该页正文，继续后续步骤
    if (error.isEmpty() && !result.text.isEmpty())
        m_fetched.append(result);
    ++m_fetchIndex;
    startNextFetch();
}

void WebSearchService::startNextFetch()
{
    if (!m_running)
        return;
    if (m_fetchIndex >= qMin(kFetchTopN, m_results.size())) {
        assembleAndFinish();
        return;
    }
    m_provider->fetch(m_results.at(m_fetchIndex).url);
}

void WebSearchService::assembleAndFinish()
{
    const QString context = buildContext();
    if (context.isEmpty()) {
        finishWithError(QStringLiteral("未检索到可用资料"));
        return;
    }
    m_running = false;
    emit finished(context, m_results, QString());
}

void WebSearchService::finishWithError(const QString &message)
{
    m_running = false;
    emit finished(QString(), QList<WebSearchResult>(), message);
}

QString WebSearchService::buildContext() const
{
    if (m_results.isEmpty())
        return QString();

    QStringList blocks;
    blocks << QStringLiteral("以下是从互联网实时检索到的资料，供你回答用户问题时参考："
                             "内容可能不完整或已过时，请甄别后使用；与问题无关时可忽略。");

    for (int i = 0; i < m_results.size(); ++i) {
        const WebSearchResult &result = m_results.at(i);
        QStringList lines;
        lines << QStringLiteral("[%1] %2").arg(i + 1).arg(result.title);
        lines << QStringLiteral("链接：%1").arg(result.url);
        if (!result.snippet.isEmpty())
            lines << QStringLiteral("摘要：%1").arg(result.snippet);
        for (const WebFetchResult &page : m_fetched) {
            if (page.url == result.url && !page.text.isEmpty()) {
                lines << QStringLiteral("正文：%1").arg(page.text);
                break;
            }
        }
        blocks << lines.join(QLatin1Char('\n'));
    }
    return blocks.join(QStringLiteral("\n\n"));
}
