#include "core/web/HttpWebSearchProvider.h"
#include "core/web/WebSearchService.h"
#include "platform/impl/DesktopPlatformAdapter.h"
#include "storage/SecureStore.h"
#include "storage/WebConfigStore.h"
#include "tests/fakes/FakeNetworkTransport.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 联网检索模块（v0.2.3 新模块）测试。
 *
 * 全部离线：网络经 FakeNetworkTransport 注入，配置只写临时目录，
 * 覆盖「端点解析 / 异常流不崩 / 服务编排 / 失败降级」四类边界。
 */
class TstWebSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // 配置存储
    void configRoundTripAndPreservesOtherSections();
    void configCorruptFallsBackWithoutCrash();

    // 检索后端（HttpWebSearchProvider）
    void providerBuildsSearchUrlWithPlaceholder();
    void providerAppendsQueryParamWhenNoPlaceholder();
    void providerSendsBearerHeaderWhenKeyPresent();
    void providerRequiresEndpoint();
    void providerParsesObjectWrappedResults();
    void providerParsesTopLevelArray();
    void providerReportsUnparseableBody();
    void providerSearchFailuresAreReported();
    void providerFetchExtractsTitleAndText();
    void providerFetchRejectsNonHttpUrl();
    void providerCancelIsSilent();

    // DeepSeek 内置协议（v0.2.4：厂商内置搜索，复用厂商 Key）
    void providerDeepSeekSearchUsesPostJson();
    void providerDeepSeekFetchUsesPostJsonAndMarkdown();
    void providerDeepSeekFetchRequiresFetchEndpoint();

    // 服务编排（WebSearchService）
    void serviceStatusExplainsMissingEndpoint();
    void serviceStatusExplainsMissingKey();
    void serviceRunsSearchThenFetchAndAssemblesContext();
    void serviceRuntimeConfigEnablesZeroConfigPath();

private:
    QString configPath(const QTemporaryDir &dir) const
    {
        return QDir(dir.path()).filePath(QStringLiteral("config.json"));
    }
};

void TstWebSearch::initTestCase()
{
    // QSignalSpy 需要能构造这些参数
    qRegisterMetaType<WebSearchResult>("WebSearchResult");
    qRegisterMetaType<QList<WebSearchResult>>("QList<WebSearchResult>");
    qRegisterMetaType<WebFetchResult>("WebFetchResult");
}

void TstWebSearch::configRoundTripAndPreservesOtherSections()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = configPath(dir);

    // 预置其他模块的段，验证 web 段写入不会覆盖它们
    QJsonObject other;
    other.insert(QStringLiteral("ai"), QJsonObject{{QStringLiteral("providerId"), QStringLiteral("deepseek")}});
    other.insert(QStringLiteral("theme"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("theme2")}});
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(other).toJson());
    file.close();

    WebConfigStore store(path);
    WebConfigData data;
    data.enabled = true;
    data.endpoint = QStringLiteral("https://search.example/api?q={query}");

    QString error;
    QVERIFY(store.save(data, &error));
    QVERIFY(error.isEmpty());

    const WebConfigData loaded = store.load(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.enabled, true);
    QCOMPARE(loaded.endpoint, data.endpoint);

    QFile check(path);
    QVERIFY(check.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(check.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("ai")).toObject().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("deepseek"));
    QCOMPARE(root.value(QStringLiteral("theme")).toObject().value(QStringLiteral("preset")).toString(),
             QStringLiteral("theme2"));
}

void TstWebSearch::configCorruptFallsBackWithoutCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = configPath(dir);

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json ]");
    file.close();

    WebConfigStore store(path);
    QString error;
    const WebConfigData data = store.load(&error);
    QVERIFY(!error.isEmpty());        // 明确告知损坏
    QVERIFY(data.endpoint.isEmpty());  // 但回退默认，不崩溃

    WebConfigData fresh;
    fresh.endpoint = QStringLiteral("https://ok.example/?q={query}");
    QVERIFY(store.save(fresh, &error));  // 在损坏文件上以空对象重建
    QCOMPARE(store.load(&error).endpoint, fresh.endpoint);
}

void TstWebSearch::providerBuildsSearchUrlWithPlaceholder()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api?q={query}&format=json");
    provider.configure(config);

    provider.search(QStringLiteral("今天 天气"), 3);

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(request.method, QStringLiteral("GET"));
    QVERIFY(request.url.contains(QStringLiteral("今")) == false);  // 关键词已 URL 编码
    QVERIFY(request.url.startsWith(QStringLiteral("https://search.example/api?q=")));
    QVERIFY(request.url.contains(QStringLiteral("&format=json")));

    provider.cancel();
}

void TstWebSearch::providerAppendsQueryParamWhenNoPlaceholder()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api");
    provider.configure(config);

    provider.search(QStringLiteral("hello"), 3);

    QCOMPARE(transport.m_requests.size(), 1);
    QVERIFY(transport.m_requests.first().url.startsWith(QStringLiteral("https://search.example/api?q=")));

    provider.cancel();
}

void TstWebSearch::providerSendsBearerHeaderWhenKeyPresent()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api");
    config.apiKey = QStringLiteral("test-search-key");
    provider.configure(config);

    provider.search(QStringLiteral("hello"), 3);

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(request.url.contains(QStringLiteral("test-search-key")), false);  // 密钥绝不进 URL
    bool hasAuth = false;
    for (const auto &header : request.headers) {
        if (header.first.compare(QStringLiteral("Authorization"), Qt::CaseInsensitive) == 0)
            hasAuth = header.second == QStringLiteral("Bearer test-search-key");
    }
    QVERIFY(hasAuth);

    provider.cancel();
}

void TstWebSearch::providerRequiresEndpoint()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    provider.configure(WebSearchConfig());

    QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
    provider.search(QStringLiteral("hello"), 3);

    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());
    QCOMPARE(transport.m_requests.size(), 0);  // 空端点绝不发请求
}

void TstWebSearch::providerParsesObjectWrappedResults()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api?q={query}");
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
    provider.search(QStringLiteral("news"), 5);
    const int handle = transport.m_lastHandle;

    transport.pushChunk(handle,
                        R"({"results":[{"title":"A","url":"https://a.example/1","snippet":"摘要A"},)"
                        R"({"name":"B","link":"https://b.example/2","description":"摘要B"},)"
                        R"({"title":"无链接"}]})");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().isEmpty());
    const auto results = spy.at(0).at(0).value<QList<WebSearchResult>>();
    QCOMPARE(results.size(), 2);  // 无 url 的条目被丢弃
    QCOMPARE(results.at(0).title, QStringLiteral("A"));
    QCOMPARE(results.at(0).url, QStringLiteral("https://a.example/1"));
    QCOMPARE(results.at(0).snippet, QStringLiteral("摘要A"));
    QCOMPARE(results.at(1).title, QStringLiteral("B"));
    QCOMPARE(results.at(1).url, QStringLiteral("https://b.example/2"));
}

void TstWebSearch::providerParsesTopLevelArray()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api?q={query}");
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
    provider.search(QStringLiteral("news"), 1);
    const int handle = transport.m_lastHandle;

    transport.pushChunk(handle, R"([{"title":"A","url":"https://a.example/1"},{"title":"B","url":"https://b.example/2"}])");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    const auto results = spy.at(0).at(0).value<QList<WebSearchResult>>();
    QCOMPARE(results.size(), 1);  // topK 生效
    QCOMPARE(results.at(0).title, QStringLiteral("A"));
}

void TstWebSearch::providerReportsUnparseableBody()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api?q={query}");
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
    provider.search(QStringLiteral("news"), 5);
    const int handle = transport.m_lastHandle;

    transport.pushChunk(handle, "<html><body>这不是 JSON</body></html>");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());  // 明确报告无法解析，不崩
    QVERIFY(spy.at(0).at(0).value<QList<WebSearchResult>>().isEmpty());
}

void TstWebSearch::providerSearchFailuresAreReported()
{
    const QList<QPair<int, NetworkTransport::Failure>> cases{
        {401, NetworkTransport::Failure::Other},
        {403, NetworkTransport::Failure::Other},
        {429, NetworkTransport::Failure::Other},
        {500, NetworkTransport::Failure::Other},
        {0, NetworkTransport::Failure::Timeout},
        {0, NetworkTransport::Failure::NetworkUnavailable},
    };

    for (const auto &entry : cases) {
        FakeNetworkTransport transport;
        HttpWebSearchProvider provider(&transport);
        WebSearchConfig config;
        config.endpoint = QStringLiteral("https://search.example/api?q={query}");
        provider.configure(config);

        QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
        provider.search(QStringLiteral("news"), 5);
        transport.pushFailed(transport.m_lastHandle, entry.first, entry.second,
                             QStringLiteral("boom"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(!spy.at(0).at(1).toString().isEmpty());
        QVERIFY(spy.at(0).at(0).value<QList<WebSearchResult>>().isEmpty());
    }
}

void TstWebSearch::providerFetchExtractsTitleAndText()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api?q={query}");
    config.maxFetchChars = 20;
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::fetchFinished);
    provider.fetch(QStringLiteral("https://page.example/1"));
    const int handle = transport.m_lastHandle;

    QVERIFY(transport.m_requests.first().url == QStringLiteral("https://page.example/1"));
    transport.pushChunk(handle,
                        "<html><head><title>页面标题</title><style>p{color:red}</style></head>"
                        "<body><script>var x=1;</script><p>第一段正文</p><p>第二段正文</p>"
                        "<p>第三段正文</p></body></html>");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().isEmpty());
    const WebFetchResult result = spy.at(0).at(0).value<WebFetchResult>();
    QCOMPARE(result.url, QStringLiteral("https://page.example/1"));
    QCOMPARE(result.title, QStringLiteral("页面标题"));
    QVERIFY(result.text.contains(QStringLiteral("第一段正文")));
    QVERIFY(result.text.contains(QStringLiteral("var x=1")) == false);  // 脚本被剥离
    QVERIFY(result.text.contains(QStringLiteral("color:red")) == false);  // 样式被剥离
    QVERIFY(result.text.endsWith(QStringLiteral("…（正文已截断）")));  // 已按上限裁剪
    QVERIFY(result.text.size() <= 30);
}

void TstWebSearch::providerFetchRejectsNonHttpUrl()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    provider.configure(WebSearchConfig{QStringLiteral("https://search.example/api"), QString(), 15000, 6000});

    QSignalSpy spy(&provider, &WebSearchProvider::fetchFinished);
    provider.fetch(QStringLiteral("file:///etc/passwd"));

    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());
    QCOMPARE(transport.m_requests.size(), 0);
}

void TstWebSearch::providerCancelIsSilent()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.endpoint = QStringLiteral("https://search.example/api?q={query}");
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
    provider.search(QStringLiteral("news"), 5);
    const int handle = transport.m_lastHandle;

    provider.cancel();
    QVERIFY(transport.m_abortedHandles.contains(handle));
    QCOMPARE(spy.count(), 0);  // 主动取消不产生任何结果信号

    // 中断后传输层再回调也不应触发信号
    transport.pushFinished(handle);
    QCOMPARE(spy.count(), 0);
}

void TstWebSearch::providerDeepSeekSearchUsesPostJson()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.protocol = WebSearchConfig::Protocol::DeepSeek;
    config.endpoint = QStringLiteral("https://api.deepseek.com/v1/tools/search");
    config.apiKey = QStringLiteral("test-ds-key");
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::searchFinished);
    provider.search(QStringLiteral("今天 天气"), 3);

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(request.method, QStringLiteral("POST"));
    // 关键词进 body，绝不进 URL；密钥也不进 URL
    QCOMPARE(request.url, QStringLiteral("https://api.deepseek.com/v1/tools/search"));
    QCOMPARE(request.url.contains(QStringLiteral("test-ds-key")), false);

    bool hasAuth = false;
    bool hasJsonContentType = false;
    for (const auto &header : request.headers) {
        if (header.first.compare(QStringLiteral("Authorization"), Qt::CaseInsensitive) == 0)
            hasAuth = header.second == QStringLiteral("Bearer test-ds-key");
        if (header.first.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0)
            hasJsonContentType = header.second.contains(QStringLiteral("application/json"));
    }
    QVERIFY(hasAuth);
    QVERIFY(hasJsonContentType);

    const QJsonObject payload = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(payload.value(QStringLiteral("query")).toString(), QStringLiteral("今天 天气"));
    QCOMPARE(payload.value(QStringLiteral("count")).toInt(), 3);

    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle,
                        R"({"results":[{"title":"A","url":"https://a.example/1","summary":"摘要summary","content":"正文content"},)"
                        R"({"title":"B","url":"https://b.example/2","content":"正文B"}]})");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().isEmpty());
    const auto results = spy.at(0).at(0).value<QList<WebSearchResult>>();
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.at(0).title, QStringLiteral("A"));
    QCOMPARE(results.at(0).snippet, QStringLiteral("摘要summary"));  // summary 优先于 content
    QCOMPARE(results.at(1).snippet, QStringLiteral("正文B"));        // 无 summary 时退回 content
}

void TstWebSearch::providerDeepSeekFetchUsesPostJsonAndMarkdown()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.protocol = WebSearchConfig::Protocol::DeepSeek;
    config.endpoint = QStringLiteral("https://api.deepseek.com/v1/tools/search");
    config.fetchEndpoint = QStringLiteral("https://api.deepseek.com/v1/tools/fetch");
    config.apiKey = QStringLiteral("test-ds-key");
    config.maxFetchChars = 20;
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::fetchFinished);
    provider.fetch(QStringLiteral("https://page.example/1"));

    QCOMPARE(transport.m_requests.size(), 1);
    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.url, QStringLiteral("https://api.deepseek.com/v1/tools/fetch"));
    QCOMPARE(QJsonDocument::fromJson(request.body).object().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://page.example/1"));

    const int handle = transport.m_lastHandle;
    transport.pushChunk(handle, R"({"title":"页面标题","content":"123456789012345678901234567890"})");
    transport.pushFinished(handle);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().isEmpty());
    const WebFetchResult result = spy.at(0).at(0).value<WebFetchResult>();
    QCOMPARE(result.url, QStringLiteral("https://page.example/1"));
    QCOMPARE(result.title, QStringLiteral("页面标题"));
    QVERIFY(result.text.startsWith(QStringLiteral("12345678901234567890")));
    QVERIFY(result.text.endsWith(QStringLiteral("…（正文已截断）")));
}

void TstWebSearch::providerDeepSeekFetchRequiresFetchEndpoint()
{
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchConfig config;
    config.protocol = WebSearchConfig::Protocol::DeepSeek;
    config.endpoint = QStringLiteral("https://api.deepseek.com/v1/tools/search");
    // fetchEndpoint 留空：内置协议下抓取端点必填
    provider.configure(config);

    QSignalSpy spy(&provider, &WebSearchProvider::fetchFinished);
    provider.fetch(QStringLiteral("https://page.example/1"));

    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());
    QCOMPARE(transport.m_requests.size(), 0);  // 缺端点绝不发请求
}

void TstWebSearch::serviceStatusExplainsMissingEndpoint()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebConfigStore configStore(configPath(dir));
    DesktopPlatformAdapter platform;
    SecureStore secure(&platform, dir.path());
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchService service(&provider, &secure, &configStore);

    QVERIFY(!service.configured());
    QVERIFY(service.statusMessage().contains(QStringLiteral("搜索端点")));

    // 未配置时 run() 直接降级为明确错误，不发任何请求
    QSignalSpy spy(&service, &WebSearchService::finished);
    service.run(QStringLiteral("随便问一句"));
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(2).toString().isEmpty());
    QCOMPARE(transport.m_requests.size(), 0);
}

void TstWebSearch::serviceStatusExplainsMissingKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebConfigStore configStore(configPath(dir));
    WebConfigData data;
    data.endpoint = QStringLiteral("https://search.example/api?q={query}");
    QString error;
    QVERIFY(configStore.save(data, &error));

    DesktopPlatformAdapter platform;
    SecureStore secure(&platform, dir.path());
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchService service(&provider, &secure, &configStore);

    const QString message = service.statusMessage();
    QVERIFY(!service.configured());
    if (secure.isAvailable())
        QVERIFY(message.contains(QStringLiteral("搜索密钥")));
    else
        QVERIFY(message.contains(QStringLiteral("安全存储")));
}

void TstWebSearch::serviceRunsSearchThenFetchAndAssemblesContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DesktopPlatformAdapter platform;
    SecureStore secure(&platform, dir.path());
    if (!secure.isAvailable()) {
        QSKIP("当前平台无系统级加密能力，跳过服务端到端编排");
    }

    WebConfigStore configStore(configPath(dir));
    WebConfigData data;
    data.enabled = true;
    data.endpoint = QStringLiteral("https://search.example/api?q={query}");
    QString error;
    QVERIFY(configStore.save(data, &error));
    QVERIFY(secure.save(SecureStore::webSearchApiKeyId(), QStringLiteral("test-search-key"), &error));

    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchService service(&provider, &secure, &configStore);
    QVERIFY(service.configured());

    QSignalSpy spy(&service, &WebSearchService::finished);
    service.run(QStringLiteral("今天的新闻"));

    // ① 先检索
    QCOMPARE(transport.m_requests.size(), 1);
    QVERIFY(transport.m_requests.first().url.contains(QStringLiteral("q=")));
    const int searchHandle = transport.m_lastHandle;
    transport.pushChunk(searchHandle,
                        R"({"results":[{"title":"A 新闻","url":"https://a.example/1","snippet":"摘要A"},)"
                        R"({"title":"B 新闻","url":"https://b.example/2","snippet":"摘要B"}]})");
    transport.pushFinished(searchHandle);

    // ② 自动抓取第 1 条正文
    QCOMPARE(transport.m_requests.size(), 2);
    QCOMPARE(transport.m_requests.at(1).url, QStringLiteral("https://a.example/1"));
    transport.pushChunk(transport.m_lastHandle,
                        "<html><head><title>A 页</title></head><body><p>正文一</p></body></html>");
    transport.pushFinished(transport.m_lastHandle);

    // ③ 抓取第 2 条失败：只跳过该页正文，不影响整体
    QCOMPARE(transport.m_requests.size(), 3);
    QCOMPARE(transport.m_requests.at(2).url, QStringLiteral("https://b.example/2"));
    transport.pushFailed(transport.m_lastHandle, 0, NetworkTransport::Failure::Timeout,
                         QStringLiteral("timeout"));

    // ④ 编排结束：上下文含标题 / 链接 / 摘要 / 已抓正文
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(2).toString().isEmpty());
    const QString context = spy.at(0).at(0).toString();
    QVERIFY(context.contains(QStringLiteral("A 新闻")));
    QVERIFY(context.contains(QStringLiteral("https://b.example/2")));
    QVERIFY(context.contains(QStringLiteral("摘要B")));
    QVERIFY(context.contains(QStringLiteral("正文一")));

    const auto sources = spy.at(0).at(1).value<QList<WebSearchResult>>();
    QCOMPARE(sources.size(), 2);
}

void TstWebSearch::serviceRuntimeConfigEnablesZeroConfigPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DesktopPlatformAdapter platform;
    SecureStore secure(&platform, dir.path());
    if (!secure.isAvailable()) {
        QSKIP("当前平台无系统级加密能力，跳过内置源零配置端到端编排");
    }

    // 不写 WebConfigStore / 不存搜索密钥：完全依赖运行时注入的方案
    WebConfigStore configStore(configPath(dir));
    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    WebSearchService service(&provider, &secure, &configStore);

    QVERIFY(!service.configured());  // 运行时方案未注入前仍未就绪

    WebSearchConfig runtime;
    runtime.protocol = WebSearchConfig::Protocol::DeepSeek;
    runtime.endpoint = QStringLiteral("https://api.deepseek.com/v1/tools/search");
    runtime.fetchEndpoint = QStringLiteral("https://api.deepseek.com/v1/tools/fetch");
    runtime.apiKey = QStringLiteral("ds-key");
    service.configure(runtime);

    QVERIFY(service.configured());  // 复用厂商 Key → 零配置即就绪
    QVERIFY(service.statusMessage().isEmpty());

    QSignalSpy spy(&service, &WebSearchService::finished);
    service.run(QStringLiteral("物业 施工规范"));

    // ① 检索：POST DeepSeek 端点（无需另填搜索密钥）
    QCOMPARE(transport.m_requests.size(), 1);
    QCOMPARE(transport.m_requests.first().method, QStringLiteral("POST"));
    QCOMPARE(transport.m_requests.first().url,
             QStringLiteral("https://api.deepseek.com/v1/tools/search"));
    transport.pushChunk(transport.m_lastHandle,
                        R"({"results":[{"title":"A 规范","url":"https://a.example/1","summary":"摘要A"},)"
                        R"({"title":"B 规范","url":"https://b.example/2","summary":"摘要B"}]})");
    transport.pushFinished(transport.m_lastHandle);

    // ② 抓取第 1 条：走 DeepSeek fetch 端点，body 为 {"url"}
    QCOMPARE(transport.m_requests.size(), 2);
    QCOMPARE(transport.m_requests.at(1).url,
             QStringLiteral("https://api.deepseek.com/v1/tools/fetch"));
    QCOMPARE(QJsonDocument::fromJson(transport.m_requests.at(1).body)
                 .object()
                 .value(QStringLiteral("url"))
                 .toString(),
             QStringLiteral("https://a.example/1"));
    transport.pushChunk(transport.m_lastHandle, R"({"title":"A 页","content":"正文一"})");
    transport.pushFinished(transport.m_lastHandle);

    // ③ 抓取第 2 条
    QCOMPARE(transport.m_requests.size(), 3);
    transport.pushChunk(transport.m_lastHandle, R"({"title":"B 页","content":"正文二"})");
    transport.pushFinished(transport.m_lastHandle);

    // ④ 拼成的上下文含标题 / 链接 / 摘要 / DeepSeek 抓回的正文
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(2).toString().isEmpty());
    const QString context = spy.at(0).at(0).toString();
    QVERIFY(context.contains(QStringLiteral("A 规范")));
    QVERIFY(context.contains(QStringLiteral("摘要B")));
    QVERIFY(context.contains(QStringLiteral("正文一")));

    // ⑤ 清空运行时方案 → 回到未配置（WebConfigStore 里没填端点）
    service.clearRuntimeConfig();
    QVERIFY(!service.configured());
    QVERIFY(!service.statusMessage().isEmpty());
}

QTEST_GUILESS_MAIN(TstWebSearch)
#include "tst_websearch.moc"
