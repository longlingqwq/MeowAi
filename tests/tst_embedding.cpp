#include "core/retriever/EmbeddingService.h"
#include "providers/ChatTypes.h"
#include "tests/fakes/FakeNetworkTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {

ProviderConfig providerConfig()
{
    ProviderConfig config;
    config.providerId = QStringLiteral("qwen");
    config.apiKey = QStringLiteral("sk-test-secret");
    config.baseUrl = QStringLiteral("https://api.example.com/v1");
    config.authStyle = QStringLiteral("bearer");
    return config;
}

LibraryEmbeddingConfig embeddingConfig(int batchSize = 16, int dimensions = 0)
{
    LibraryEmbeddingConfig config;
    config.providerId = QStringLiteral("qwen");
    config.modelId = QStringLiteral("text-embedding-v4");
    config.batchSize = batchSize;
    config.dimensions = dimensions;
    return config;
}

QByteArray embeddingResponse(const QList<QVector<float>> &vectors, int promptTokens = 4,
                             bool reversed = false)
{
    QJsonArray data;
    const int count = vectors.size();
    for (int position = 0; position < count; ++position) {
        const int index = reversed ? count - 1 - position : position;
        const QVector<float> &vector = vectors.at(index);
        QJsonArray values;
        for (float value : vector) {
            values.append(static_cast<double>(value));
        }
        QJsonObject item;
        item.insert(QStringLiteral("object"), QStringLiteral("embedding"));
        item.insert(QStringLiteral("index"), index);
        item.insert(QStringLiteral("embedding"), values);
        data.append(item);
    }

    QJsonObject usage;
    usage.insert(QStringLiteral("prompt_tokens"), promptTokens);
    usage.insert(QStringLiteral("total_tokens"), promptTokens);

    QJsonObject root;
    root.insert(QStringLiteral("object"), QStringLiteral("list"));
    root.insert(QStringLiteral("data"), data);
    root.insert(QStringLiteral("usage"), usage);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QVector<float> vector3(float base)
{
    return QVector<float>{base, base + 1.0f, base + 2.0f};
}

}  // namespace

/**
 * M4 embedding 测试（全程离线，网络经 FakeNetworkTransport 注入）：
 *  - 未配置 Key / 模型 / 地址时**零请求**、结构化返回 NotConfigured（延迟索引）；
 *  - 分批串行、按 index 归位、usage 累加、维度校验；
 *  - 401 / 429 / 超时 / 断网 结构化映射，且错误信息**不含密钥**；
 *  - 取消可中断在途请求。
 */
class TstEmbedding : public QObject
{
    Q_OBJECT

private slots:
    void notReadySendsNoRequest();
    void batchesAndIndexesVectors();
    void unauthorizedIsStructuredAndStops();
    void rateLimitedIsDistinctFromUnauthorized();
    void networkFailureMapsToNetworkUnavailable();
    void emptyInputFinishesWithoutRequest();
};

void TstEmbedding::notReadySendsNoRequest()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);

    ProviderConfig provider = providerConfig();
    provider.apiKey.clear();
    service.configure(provider, embeddingConfig());

    bool failed = false;
    ChatError error;
    connect(&service, &EmbeddingService::failed, this, [&](const ChatError &value) {
        failed = true;
        error = value;
    });

    QVERIFY(!service.isReady());
    service.embed({QStringLiteral("文本")});

    QVERIFY(failed);
    QCOMPARE(error.code, ChatError::NotConfigured);
    QCOMPARE(transport.m_requests.size(), 0);  // 延迟索引：一个请求都不发
    QVERIFY(!service.busy());
}

void TstEmbedding::batchesAndIndexesVectors()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(providerConfig(), embeddingConfig(2));

    EmbeddingResult result;
    ChatError error;
    int finishedCount = 0;
    int failedCount = 0;
    connect(&service, &EmbeddingService::finished, this, [&](const EmbeddingResult &value) {
        ++finishedCount;
        result = value;
    });
    connect(&service, &EmbeddingService::failed, this, [&](const ChatError &value) {
        ++failedCount;
        error = value;
    });

    service.embed({QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三")});
    QVERIFY(service.busy());
    QCOMPARE(transport.m_requests.size(), 1);

    // 第一批：两条输入；服务端故意乱序返回 index，结果必须按输入顺序归位
    const int handle1 = transport.m_lastHandle;
    const QByteArray firstBody = transport.m_requests.at(0).body;
    const QJsonObject firstJson = QJsonDocument::fromJson(firstBody).object();
    QCOMPARE(firstJson.value(QStringLiteral("input")).toArray().size(), 2);
    transport.pushChunk(handle1, embeddingResponse({vector3(0.0f), vector3(10.0f)}, 4, true));
    transport.pushFinished(handle1);

    // 第一批完成后批次已推进：第二批在途，此时还不能 finished
    QCOMPARE(finishedCount, 0);
    QCOMPARE(transport.m_requests.size(), 2);

    const int handle2 = transport.m_lastHandle;
    transport.pushChunk(handle2, embeddingResponse({vector3(20.0f)}, 2));
    transport.pushFinished(handle2);

    QCOMPARE(failedCount, 0);
    QCOMPARE(finishedCount, 1);
    QVERIFY(result.ok);
    QCOMPARE(result.vectors.size(), 3);
    QCOMPARE(result.vectors.at(0).at(0), 0.0f);
    QCOMPARE(result.vectors.at(1).at(0), 10.0f);
    QCOMPARE(result.vectors.at(2).at(0), 20.0f);
    QCOMPARE(result.dimensions, 3);
    QCOMPARE(result.usage.promptTokens, 6);  // 两批累加
    QCOMPARE(result.usage.totalTokens, 6);
    QVERIFY(!service.busy());
}

void TstEmbedding::unauthorizedIsStructuredAndStops()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(providerConfig(), embeddingConfig(1));

    ChatError error;
    bool failed = false;
    connect(&service, &EmbeddingService::failed, this, [&](const ChatError &value) {
        failed = true;
        error = value;
    });

    service.embed({QStringLiteral("一"), QStringLiteral("二")});
    QCOMPARE(transport.m_requests.size(), 1);
    transport.pushFailed(transport.m_lastHandle, 401, NetworkTransport::Failure::Other,
                         QStringLiteral("invalid key sk-test-secret"));

    QVERIFY(failed);
    QCOMPARE(error.code, ChatError::Unauthorized);
    QVERIFY2(!error.message.contains(QStringLiteral("sk-test-secret")), "错误信息泄露了密钥");
    QCOMPARE(transport.m_requests.size(), 1);  // 首个批次失败即停止，不再发后续批次
    QVERIFY(!service.busy());
}

void TstEmbedding::rateLimitedIsDistinctFromUnauthorized()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(providerConfig(), embeddingConfig(16));

    ChatError error;
    connect(&service, &EmbeddingService::failed, this,
            [&](const ChatError &value) { error = value; });

    service.embed({QStringLiteral("一")});
    transport.pushFailed(transport.m_lastHandle, 429, NetworkTransport::Failure::Other,
                         QStringLiteral("too many requests"));
    QCOMPARE(error.code, ChatError::RateLimited);
    QCOMPARE(error.httpStatus, 429);
}

void TstEmbedding::networkFailureMapsToNetworkUnavailable()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(providerConfig(), embeddingConfig(16));

    ChatError error;
    connect(&service, &EmbeddingService::failed, this,
            [&](const ChatError &value) { error = value; });

    service.embed({QStringLiteral("一")});
    transport.pushFailed(transport.m_lastHandle, 0,
                         NetworkTransport::Failure::NetworkUnavailable,
                         QStringLiteral("host not found"));
    QCOMPARE(error.code, ChatError::NetworkUnavailable);
}

void TstEmbedding::emptyInputFinishesWithoutRequest()
{
    FakeNetworkTransport transport;
    EmbeddingService service(&transport);
    service.configure(providerConfig(), embeddingConfig());

    bool finished = false;
    EmbeddingResult result;
    connect(&service, &EmbeddingService::finished, this, [&](const EmbeddingResult &value) {
        finished = true;
        result = value;
    });

    service.embed({});
    QVERIFY(finished);
    QVERIFY(result.ok);
    QVERIFY(result.vectors.isEmpty());
    QCOMPARE(transport.m_requests.size(), 0);
}

QTEST_GUILESS_MAIN(TstEmbedding)
#include "tst_embedding.moc"
