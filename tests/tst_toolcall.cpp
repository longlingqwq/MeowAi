#include "core/ai/ProviderCatalog.h"
#include "core/chat/ChatController.h"
#include "core/web/HttpWebSearchProvider.h"
#include "core/web/WebSearchService.h"
#include "core/web/WebSearchTool.h"
#include "platform/impl/DesktopPlatformAdapter.h"
#include "providers/OpenAiProvider.h"
#include "storage/AiConfigStore.h"
#include "storage/SecureStore.h"
#include "storage/WebConfigStore.h"
#include "tests/fakes/FakeNetworkTransport.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>

namespace {

ProviderConfig readyConfig()
{
    ProviderConfig config;
    config.apiKey = QStringLiteral("test-key");
    config.model = QStringLiteral("test-model");
    return config;
}

/// 带「联网搜索」工具的请求（非流式 / 流式由参数决定）
ChatRequest toolRequest(bool stream)
{
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = QStringLiteral("今天有什么新闻");

    ChatRequest request;
    request.model = QStringLiteral("test-model");
    request.stream = stream;
    request.messages.append(message);
    request.tools = QJsonArray{WebSearchTool::descriptor()};
    request.toolChoice = QStringLiteral("auto");
    return request;
}

QByteArray sseChunk(const QJsonObject &delta, const QString &finishReason = QString())
{
    QJsonObject choice;
    choice.insert(QStringLiteral("delta"), delta);
    if (!finishReason.isEmpty())
        choice.insert(QStringLiteral("finish_reason"), finishReason);
    QJsonObject root;
    root.insert(QStringLiteral("choices"), QJsonArray{choice});
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact)
        + QByteArrayLiteral("\n\n");
}

/// 推送一轮「模型要求调用 web_search」的流式分片（arguments 故意拆成两片，验证拼接）
void pushToolCallRound(FakeNetworkTransport &transport, int handle, const QString &query,
                       const QString &callId)
{
    QJsonObject firstCall;
    firstCall.insert(QStringLiteral("index"), 0);
    firstCall.insert(QStringLiteral("id"), callId);
    firstCall.insert(QStringLiteral("type"), QStringLiteral("function"));
    QJsonObject firstFunction;
    firstFunction.insert(QStringLiteral("name"), QStringLiteral("web_search"));
    firstFunction.insert(QStringLiteral("arguments"), QStringLiteral("{\"query\":\""));
    firstCall.insert(QStringLiteral("function"), firstFunction);

    QJsonObject secondCall;
    secondCall.insert(QStringLiteral("index"), 0);
    QJsonObject secondFunction;
    secondFunction.insert(QStringLiteral("arguments"), query + QStringLiteral("\"}"));
    secondCall.insert(QStringLiteral("function"), secondFunction);

    QJsonObject firstDelta;
    firstDelta.insert(QStringLiteral("tool_calls"), QJsonArray{firstCall});
    QJsonObject secondDelta;
    secondDelta.insert(QStringLiteral("tool_calls"), QJsonArray{secondCall});

    transport.pushChunk(handle, sseChunk(firstDelta));
    transport.pushChunk(handle, sseChunk(secondDelta));
    transport.pushChunk(handle, sseChunk(QJsonObject(), QStringLiteral("tool_calls")));
    transport.pushChunk(handle, QByteArrayLiteral("data: [DONE]\n\n"));
    transport.pushFinished(handle);
}

/// 推送一轮普通文本回答
void pushTextRound(FakeNetworkTransport &transport, int handle, const QString &text)
{
    QJsonObject delta;
    delta.insert(QStringLiteral("content"), text);
    transport.pushChunk(handle, sseChunk(delta));
    transport.pushChunk(handle, sseChunk(QJsonObject(), QStringLiteral("stop")));
    transport.pushChunk(handle, QByteArrayLiteral("data: [DONE]\n\n"));
    transport.pushFinished(handle);
}

/// 推送一次完整检索：搜索（1 条结果）+ 抓取正文
void pushSearchRound(FakeNetworkTransport &transport)
{
    const int searchHandle = transport.m_lastHandle;
    transport.pushChunk(searchHandle,
                        QByteArrayLiteral("{\"results\":[{\"title\":\"A\",\"url\":\"https://a.example/1\","
                                          "\"summary\":\"摘要A\"}]}"));
    transport.pushFinished(searchHandle);

    const int fetchHandle = transport.m_lastHandle;
    transport.pushChunk(fetchHandle,
                        QByteArrayLiteral("{\"title\":\"A 页\",\"content\":\"正文一\"}"));
    transport.pushFinished(fetchHandle);
}

/// 写一份测试用 providers.json（单厂商 deepseek，可选内置搜索）
QString writeProvidersFile(const QTemporaryDir &dir, bool withNativeSearch)
{
    QJsonObject model;
    model.insert(QStringLiteral("id"), QStringLiteral("deepseek-chat"));
    model.insert(QStringLiteral("displayName"), QStringLiteral("DeepSeek Chat"));
    model.insert(QStringLiteral("isDefault"), true);

    QJsonObject provider;
    provider.insert(QStringLiteral("id"), QStringLiteral("deepseek"));
    provider.insert(QStringLiteral("displayName"), QStringLiteral("DeepSeek"));
    provider.insert(QStringLiteral("protocol"), QStringLiteral("openai"));
    provider.insert(QStringLiteral("baseUrl"), QStringLiteral("https://api.deepseek.com/v1"));
    provider.insert(QStringLiteral("authStyle"), QStringLiteral("bearer"));
    provider.insert(QStringLiteral("validation"),
                    QJsonObject{{QStringLiteral("method"), QStringLiteral("models")}});
    if (withNativeSearch) {
        QJsonObject nativeSearch;
        nativeSearch.insert(QStringLiteral("protocol"), QStringLiteral("deepseek"));
        nativeSearch.insert(QStringLiteral("searchEndpoint"),
                            QStringLiteral("https://api.deepseek.com/v1/tools/search"));
        nativeSearch.insert(QStringLiteral("fetchEndpoint"),
                            QStringLiteral("https://api.deepseek.com/v1/tools/fetch"));
        nativeSearch.insert(QStringLiteral("reuseProviderKey"), true);
        provider.insert(QStringLiteral("nativeSearch"), nativeSearch);
    }
    provider.insert(QStringLiteral("models"), QJsonArray{model});

    QJsonObject root;
    root.insert(QStringLiteral("version"), QStringLiteral("test"));
    root.insert(QStringLiteral("providers"), QJsonArray{provider});

    const QString path = dir.filePath(QStringLiteral("providers.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

/// 组装 ChatController 全链路（含可注入的假网络），用于离线覆盖工具循环
struct ChatFixture
{
    QTemporaryDir dir;
    DesktopPlatformAdapter platform;
    FakeNetworkTransport providerTransport;
    FakeNetworkTransport webTransport;

    std::unique_ptr<SecureStore> store;
    std::unique_ptr<ProviderCatalog> catalog;
    std::unique_ptr<AiConfigStore> aiConfig;
    std::unique_ptr<WebConfigStore> webConfig;
    std::unique_ptr<HttpWebSearchProvider> webProvider;
    std::unique_ptr<WebSearchService> webService;
    std::unique_ptr<ChatController> chat;

    bool build(bool withNativeSearch = true)
    {
        if (!dir.isValid())
            return false;

        store = std::make_unique<SecureStore>(&platform, dir.path());
        if (!store->isAvailable())
            return false;  // 平台不支持安全存储：调用方 QSKIP

        QString error;
        if (!store->save(SecureStore::providerApiKeyId(QStringLiteral("deepseek")),
                         QStringLiteral("sk-deepseek"), &error))
            return false;

        catalog = std::make_unique<ProviderCatalog>(writeProvidersFile(dir, withNativeSearch));
        if (!catalog->load())
            return false;

        const QString configPath = dir.filePath(QStringLiteral("config.json"));
        aiConfig = std::make_unique<AiConfigStore>(configPath);
        webConfig = std::make_unique<WebConfigStore>(configPath);

        webProvider = std::make_unique<HttpWebSearchProvider>(&webTransport);
        webService = std::make_unique<WebSearchService>(webProvider.get(), store.get(),
                                                        webConfig.get());
        chat = std::make_unique<ChatController>(store.get(), nullptr, catalog.get(),
                                                aiConfig.get(), webConfig.get(), webService.get(),
                                                &providerTransport);
        return true;
    }
};

}  // namespace

/// 工具调用（B 形态）全链路测试：
///  - Provider 侧：请求体下发 tools / tool_choice、流式 tool_calls 拼接、finishReason 判定；
///  - core 侧：模型**自行决定**是否联网、工具循环、轮数上限、不支持时降级为普通对话。
class TstToolCall : public QObject
{
    Q_OBJECT

private slots:
    // ---- Provider 侧 ----
    void providerSendsToolsInRequestBody();
    void providerAccumulatesStreamingToolCalls();
    void providerParsesNonStreamingToolCalls();

    // ---- 工具描述与执行 ----
    void webSearchToolDescriptorAndExecution();

    // ---- core 侧（ChatController 工具循环）----
    void controllerSkipsSearchWhenModelAnswersNormally();
    void controllerRunsSearchThenAnswers();
    void controllerStopsAtMaxToolRounds();
    void controllerDegradesWhenProviderUnsupported();
};

void TstToolCall::providerSendsToolsInRequestBody()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    provider.sendChat(toolRequest(/*stream=*/false));
    QCOMPARE(transport.m_requests.size(), 1);

    const NetworkTransport::Request request = transport.m_requests.first();
    QCOMPARE(request.method, QStringLiteral("POST"));

    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("tool_choice")).toString(), QStringLiteral("auto"));

    const QJsonArray tools = body.value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 1);
    const QJsonObject function = tools.first().toObject()
                                     .value(QStringLiteral("function")).toObject();
    QCOMPARE(function.value(QStringLiteral("name")).toString(), QStringLiteral("web_search"));

    // 普通对话（不带工具）不得下发 tools 字段
    ChatRequest plain = toolRequest(/*stream=*/false);
    plain.tools = QJsonArray();
    plain.toolChoice.clear();
    provider.sendChat(plain);
    const QJsonObject plainBody = QJsonDocument::fromJson(transport.m_requests.at(1).body).object();
    QVERIFY(!plainBody.contains(QStringLiteral("tools")));
    QVERIFY(!plainBody.contains(QStringLiteral("tool_choice")));
}

void TstToolCall::providerAccumulatesStreamingToolCalls()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);
    QSignalSpy deltaSpy(&provider, &AiProvider::chatDelta);

    provider.sendChat(toolRequest(/*stream=*/true));
    const int handle = transport.m_lastHandle;
    pushToolCallRound(transport, handle, QStringLiteral("今日 新闻"), QStringLiteral("call_abc"));

    QCOMPARE(finishedSpy.count(), 1);
    const ChatResponse response = finishedSpy.at(0).at(0).value<ChatResponse>();

    QVERIFY(response.ok);
    QVERIFY(response.text.isEmpty());  // 只调工具、不输出正文
    QCOMPARE(response.toolCalls.size(), 1);
    QCOMPARE(response.toolCalls.first().id, QStringLiteral("call_abc"));
    QCOMPARE(response.toolCalls.first().name, QStringLiteral("web_search"));
    // 分两片到达的 arguments 必须被拼接还原
    QCOMPARE(response.toolCalls.first().arguments, QStringLiteral("{\"query\":\"今日 新闻\"}"));
    QCOMPARE(response.finishReason, ChatResponse::ToolCalls);
    QCOMPARE(deltaSpy.count(), 0);
}

void TstToolCall::providerParsesNonStreamingToolCalls()
{
    FakeNetworkTransport transport;
    OpenAiProvider provider(&transport);
    provider.configure(readyConfig());

    QSignalSpy finishedSpy(&provider, &AiProvider::chatFinished);

    provider.sendChat(toolRequest(/*stream=*/false));
    const int handle = transport.m_lastHandle;

    QJsonObject function;
    function.insert(QStringLiteral("name"), QStringLiteral("web_search"));
    function.insert(QStringLiteral("arguments"), QStringLiteral("{\"query\":\"天气预报\"}"));
    QJsonObject call;
    call.insert(QStringLiteral("id"), QStringLiteral("call_ns"));
    call.insert(QStringLiteral("type"), QStringLiteral("function"));
    call.insert(QStringLiteral("function"), function);

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    message.insert(QStringLiteral("content"), QJsonValue::Null);
    message.insert(QStringLiteral("tool_calls"), QJsonArray{call});

    QJsonObject choice;
    choice.insert(QStringLiteral("message"), message);
    choice.insert(QStringLiteral("finish_reason"), QStringLiteral("tool_calls"));

    QJsonObject root;
    root.insert(QStringLiteral("choices"), QJsonArray{choice});

    transport.pushChunk(handle, QJsonDocument(root).toJson(QJsonDocument::Compact));
    transport.pushFinished(handle);

    QCOMPARE(finishedSpy.count(), 1);
    const ChatResponse response = finishedSpy.at(0).at(0).value<ChatResponse>();
    QVERIFY(response.ok);
    QCOMPARE(response.toolCalls.size(), 1);
    QCOMPARE(response.toolCalls.first().id, QStringLiteral("call_ns"));
    QCOMPARE(response.toolCalls.first().arguments, QStringLiteral("{\"query\":\"天气预报\"}"));
    QCOMPARE(response.finishReason, ChatResponse::ToolCalls);
}

void TstToolCall::webSearchToolDescriptorAndExecution()
{
    QCOMPARE(WebSearchTool::toolName(), QStringLiteral("web_search"));

    const QJsonObject descriptor = WebSearchTool::descriptor();
    QCOMPARE(descriptor.value(QStringLiteral("type")).toString(), QStringLiteral("function"));
    const QJsonObject function = descriptor.value(QStringLiteral("function")).toObject();
    QCOMPARE(function.value(QStringLiteral("name")).toString(), QStringLiteral("web_search"));
    const QJsonObject parameters = function.value(QStringLiteral("parameters")).toObject();
    QCOMPARE(parameters.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
    const QJsonArray required = parameters.value(QStringLiteral("required")).toArray();
    QCOMPARE(required.size(), 1);
    QCOMPARE(required.first().toString(), QStringLiteral("query"));

    FakeNetworkTransport transport;
    HttpWebSearchProvider provider(&transport);
    // 仅走运行时方案：不需要 config/secure 存储
    WebSearchService service(&provider, nullptr, nullptr);
    WebSearchTool tool(&service);

    WebSearchConfig config;
    config.protocol = WebSearchConfig::Protocol::DeepSeek;
    config.endpoint = QStringLiteral("https://api.deepseek.com/v1/tools/search");
    config.fetchEndpoint = QStringLiteral("https://api.deepseek.com/v1/tools/fetch");
    config.apiKey = QStringLiteral("sk-x");
    tool.configure(config);
    QVERIFY(tool.configured());
    QVERIFY(tool.statusMessage().isEmpty());

    QSignalSpy spy(&tool, &WebSearchTool::finished);
    tool.execute(QStringLiteral("今天 新闻"));
    QVERIFY(tool.busy());
    QCOMPARE(transport.m_requests.size(), 1);
    QCOMPARE(transport.m_requests.first().method, QStringLiteral("POST"));

    pushSearchRound(transport);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(2).toString().isEmpty());
    const QString resultText = spy.at(0).at(0).toString();
    QVERIFY(resultText.contains(QStringLiteral("摘要A")));
    QVERIFY(resultText.contains(QStringLiteral("正文一")));
    QVERIFY(!tool.busy());

    // 未装配 service：直接以错误回传，不抛异常
    WebSearchTool detached(nullptr);
    QSignalSpy detachedSpy(&detached, &WebSearchTool::finished);
    detached.execute(QStringLiteral("x"));
    QCOMPARE(detachedSpy.count(), 1);
    QVERIFY(!detachedSpy.at(0).at(2).toString().isEmpty());
}

void TstToolCall::controllerSkipsSearchWhenModelAnswersNormally()
{
    ChatFixture fixture;
    if (!fixture.build())
        QSKIP("当前平台无系统级加密能力，跳过联网工具循环");
    ChatController &chat = *fixture.chat;

    QVERIFY(chat.webSearchNative());
    chat.setWebSearchEnabled(true);
    QVERIFY2(chat.webSearchReady(), qPrintable(chat.webSearchMessage()));

    chat.send(QStringLiteral("你好"));
    QCOMPARE(fixture.providerTransport.m_requests.size(), 1);

    // 请求体已带工具清单：由模型自行决定是否调用
    const QJsonObject body =
        QJsonDocument::fromJson(fixture.providerTransport.m_requests.first().body).object();
    QCOMPARE(body.value(QStringLiteral("tool_choice")).toString(), QStringLiteral("auto"));
    const QJsonArray tools = body.value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 1);

    // 模型不调用工具 → 全程不发起检索
    pushTextRound(fixture.providerTransport, fixture.providerTransport.m_lastHandle,
                  QStringLiteral("你好呀"));

    QCOMPARE(fixture.webTransport.m_requests.size(), 0);
    QVERIFY(!chat.busy());
    QCOMPARE(chat.replyText(), QStringLiteral("你好呀"));
}

void TstToolCall::controllerRunsSearchThenAnswers()
{
    ChatFixture fixture;
    if (!fixture.build())
        QSKIP("当前平台无系统级加密能力，跳过联网工具循环");
    ChatController &chat = *fixture.chat;

    chat.setWebSearchEnabled(true);
    QVERIFY(chat.webSearchReady());

    chat.send(QStringLiteral("今天有什么新闻"));
    QCOMPARE(fixture.providerTransport.m_requests.size(), 1);

    // 第一轮：模型要求联网 → 控制器发起真实检索（走 web 传输）
    pushToolCallRound(fixture.providerTransport, fixture.providerTransport.m_lastHandle,
                      QStringLiteral("今日 新闻"), QStringLiteral("call_1"));
    QCOMPARE(fixture.webTransport.m_requests.size(), 1);
    QCOMPARE(fixture.webTransport.m_requests.first().method, QStringLiteral("POST"));
    QCOMPARE(fixture.webTransport.m_requests.first().url,
             QStringLiteral("https://api.deepseek.com/v1/tools/search"));

    pushSearchRound(fixture.webTransport);

    // 第二轮请求：带上 assistant 的 tool_calls 与 tool 结果
    QCOMPARE(fixture.providerTransport.m_requests.size(), 2);
    const QJsonArray messages =
        QJsonDocument::fromJson(fixture.providerTransport.m_requests.at(1).body)
            .object()
            .value(QStringLiteral("messages"))
            .toArray();

    bool sawAssistantToolCalls = false;
    bool sawToolResult = false;
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("assistant")
            && !message.value(QStringLiteral("tool_calls")).toArray().isEmpty()) {
            sawAssistantToolCalls = true;
        }
        if (role == QStringLiteral("tool")) {
            sawToolResult = true;
            QCOMPARE(message.value(QStringLiteral("tool_call_id")).toString(),
                     QStringLiteral("call_1"));
            QVERIFY(message.value(QStringLiteral("content")).toString()
                        .contains(QStringLiteral("摘要A")));
        }
    }
    QVERIFY(sawAssistantToolCalls);
    QVERIFY(sawToolResult);

    pushTextRound(fixture.providerTransport, fixture.providerTransport.m_lastHandle,
                  QStringLiteral("今天有雨"));

    QVERIFY(!chat.busy());
    QCOMPARE(chat.replyText(), QStringLiteral("今天有雨"));
    QCOMPARE(chat.webSearchSources().size(), 1);
    QVERIFY(chat.lastError().isEmpty());
}

void TstToolCall::controllerStopsAtMaxToolRounds()
{
    ChatFixture fixture;
    if (!fixture.build())
        QSKIP("当前平台无系统级加密能力，跳过联网工具循环");
    ChatController &chat = *fixture.chat;

    chat.setWebSearchEnabled(true);
    chat.send(QStringLiteral("帮我查很多资料"));

    // 模型连续 4 轮要求联网；控制器最多执行 3 轮，第 4 轮直接收尾并提示
    for (int round = 0; round < 4; ++round) {
        pushToolCallRound(fixture.providerTransport, fixture.providerTransport.m_lastHandle,
                          QStringLiteral("关键词"), QStringLiteral("call_%1").arg(round + 1));
        if (round < 3)
            pushSearchRound(fixture.webTransport);
    }

    QCOMPARE(fixture.providerTransport.m_requests.size(), 4);
    QCOMPARE(fixture.webTransport.m_requests.size(), 6);
    QVERIFY(chat.lastError().contains(QStringLiteral("最大轮数")));
    QVERIFY(!chat.busy());
}

void TstToolCall::controllerDegradesWhenProviderUnsupported()
{
    ChatFixture fixture;
    if (!fixture.build(/*withNativeSearch=*/false))
        QSKIP("当前平台无系统级加密能力，跳过联网工具循环");
    ChatController &chat = *fixture.chat;

    QVERIFY(!chat.webSearchNative());
    chat.setWebSearchEnabled(true);
    QVERIFY(!chat.webSearchReady());
    QVERIFY(chat.webSearchMessage().contains(QStringLiteral("暂不支持")));

    chat.send(QStringLiteral("你好"));
    QCOMPARE(fixture.providerTransport.m_requests.size(), 1);

    // 降级为普通对话：不下发工具，并明确提示用户
    const QJsonObject body =
        QJsonDocument::fromJson(fixture.providerTransport.m_requests.first().body).object();
    QVERIFY(!body.contains(QStringLiteral("tools")));
    QVERIFY(chat.lastError().contains(QStringLiteral("已按普通对话回答")));

    pushTextRound(fixture.providerTransport, fixture.providerTransport.m_lastHandle,
                  QStringLiteral("你好呀"));

    QCOMPARE(fixture.webTransport.m_requests.size(), 0);
    QCOMPARE(chat.replyText(), QStringLiteral("你好呀"));
}

QTEST_MAIN(TstToolCall)
#include "tst_toolcall.moc"
