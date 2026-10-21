#include "core/retriever/EmbeddingService.h"
#include "core/retriever/RagRetriever.h"
#include "storage/Database.h"
#include "storage/LibraryStore.h"
#include "storage/SqliteVectorIndex.h"
#include "tests/fakes/FakeNetworkTransport.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
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

LibraryEmbeddingConfig embeddingConfig()
{
    LibraryEmbeddingConfig config;
    config.providerId = QStringLiteral("qwen");
    config.modelId = QStringLiteral("text-embedding-v4");
    config.batchSize = 16;
    return config;
}

QByteArray embedResponse(const QVector<float> &vector)
{
    QJsonArray values;
    for (float value : vector) {
        values.append(static_cast<double>(value));
    }
    QJsonObject item;
    item.insert(QStringLiteral("index"), 0);
    item.insert(QStringLiteral("embedding"), values);
    QJsonArray data;
    data.append(item);
    QJsonObject usage;
    usage.insert(QStringLiteral("prompt_tokens"), 3);
    usage.insert(QStringLiteral("total_tokens"), 3);
    QJsonObject root;
    root.insert(QStringLiteral("data"), data);
    root.insert(QStringLiteral("usage"), usage);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

/// 每个用例一套真实 SQLite（临时目录）+ 存储 + 向量索引
class Harness
{
public:
    Harness()
    {
        QString error;
        if (!m_db.open(QDir(m_tmp.path()).filePath(QStringLiteral("metadata.db")), &error)
            || !m_db.ensureSchema(&error)) {
            m_error = error;
            return;
        }
        m_store = new LibraryStore(&m_db);
        m_index = new SqliteVectorIndex(&m_db);
    }

    ~Harness()
    {
        delete m_store;
        delete m_index;
    }

    QString error() const { return m_error; }
    LibraryStore *store() { return m_store; }
    SqliteVectorIndex *index() { return m_index; }
    const QTemporaryDir &tmp() const { return m_tmp; }

    qint64 addCollection(const QString &name)
    {
        qint64 id = 0;
        QString error;
        if (!m_store->insertCollection(name, QString(), &id, &error)) {
            m_error = error;
        }
        return id;
    }

    qint64 addFile(const QString &name, qint64 collectionId)
    {
        LibraryFileRecord file;
        file.displayName = name;
        file.sourceKind = QStringLiteral("upload");
        file.sourcePath = QDir(m_tmp.path()).filePath(name);
        file.ext = name.section(QLatin1Char('.'), -1);
        file.kind = QStringLiteral("text");
        file.status = QStringLiteral("indexed");
        qint64 id = 0;
        QString error;
        if (!m_store->insertFile(file, &id, &error)) {
            m_error = error;
            return 0;
        }
        if (!m_store->linkFileToCollection(id, collectionId, &error)) {
            m_error = error;
            return 0;
        }
        return id;
    }

    QList<qint64> addChunks(qint64 fileId, const QStringList &texts,
                            const QString &locKind = QStringLiteral("para"))
    {
        QList<ChunkRecord> chunks;
        for (int i = 0; i < texts.size(); ++i) {
            ChunkRecord chunk;
            chunk.ordinal = i;
            chunk.text = texts.at(i);
            chunk.locKind = locKind;
            chunk.locLabel = QStringLiteral("第 %1 段").arg(i + 1);
            chunk.charCount = chunk.text.size();
            chunk.tokenEst = 0;  // 让检索层自己估算，避免夹具与实现耦合
            chunks.append(chunk);
        }
        QString error;
        if (!m_store->replaceChunks(fileId, chunks, &error)) {
            m_error = error;
            return {};
        }
        QList<qint64> ids;
        for (const ChunkRecord &chunk : m_store->chunksOfFile(fileId, &error)) {
            ids.append(chunk.id);
        }
        if (!error.isEmpty()) {
            m_error = error;
        }
        return ids;
    }

private:
    QTemporaryDir m_tmp;
    Database m_db;
    LibraryStore *m_store = nullptr;
    SqliteVectorIndex *m_index = nullptr;
    QString m_error;
};

RetrievalConfig baseConfig()
{
    RetrievalConfig config;
    config.topK = 5;
    config.perFileLimit = 3;
    config.tokenBudget = 5000;
    config.similarityThreshold = 0.30;
    config.neighborRadius = 0;
    config.ftsFallback = true;
    return config;
}

}  // namespace

/**
 * M5 检索测试（真实 SQLite + 假网络，全程离线）：
 *  - 关键词兜底：embedding 未配置时**零请求**、降级标记、定位元数据完整；
 *  - 三项可配置：K / 每文件上限 / token 预算改动后行为随之改变；
 *  - 邻居扩展、相似度阈值、挂载库过滤；
 *  - 向量路径（FakeNetworkTransport 注入查询向量）与失败降级；
 *  - 同步 `retrieve()` **绝不联网**。
 */
class TstRagRetriever : public QObject
{
    Q_OBJECT

private slots:
    void keywordFallbackKeepsLocators();
    void perFileLimitCapsSingleFile();
    void neighborExpansionAddsAdjacentChunks();
    void tokenBudgetLimitsContext();
    void collectionFilterKeepsOnlyMounted();
    void emptyMountReturnsNothing();
    void vectorSearchAppliesThreshold();
    void embeddingFailureFallsBackToKeyword();
    void syncRetrieveNeverTouchesNetwork();
};

void TstRagRetriever::keywordFallbackKeepsLocators()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("物业工程"));
    const qint64 file = h.addFile(QStringLiteral("巡检手册.md"), collection);
    QVERIFY(file > 0);
    h.addChunks(file, {QStringLiteral("消防泵房巡检要求：每周检查压力表")});

    FakeNetworkTransport transport;
    EmbeddingService embedding(&transport);  // 未配置 Key → 不可用
    RagRetriever retriever(h.store(), h.index(), &embedding);
    retriever.setConfig(baseConfig());
    retriever.setMountedCollections({collection});

    bool done = false;
    RetrievalResult result;
    connect(&retriever, &RagRetriever::finished, this, [&](const RetrievalResult &value) {
        done = true;
        result = value;
    });

    retriever.retrieveAsync(QStringLiteral("消防泵房"));
    QVERIFY(done);
    QCOMPARE(transport.m_requests.size(), 0);  // 没有 embedding → 一个请求都不发
    QVERIFY(result.degraded);
    QCOMPARE(result.mode, QStringLiteral("keyword"));
    QVERIFY(!result.note.isEmpty());
    QCOMPARE(result.chunks.size(), 1);
    QCOMPARE(result.chunks.first().fileName, QStringLiteral("巡检手册.md"));
    QCOMPARE(result.chunks.first().locKind, QStringLiteral("para"));
    QCOMPARE(result.chunks.first().locLabel, QStringLiteral("第 1 段"));
    QCOMPARE(result.chunks.first().collectionId, collection);
    QVERIFY(result.chunks.first().tokenEst > 0);
    QVERIFY(result.tokenEst >= result.chunks.first().tokenEst);
}

void TstRagRetriever::perFileLimitCapsSingleFile()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("大文件库"));
    const qint64 file = h.addFile(QStringLiteral("大文件.md"), collection);
    h.addChunks(file, {QStringLiteral("巡检记录 1"), QStringLiteral("巡检记录 2"),
                       QStringLiteral("巡检记录 3"), QStringLiteral("巡检记录 4"),
                       QStringLiteral("巡检记录 5")});

    RagRetriever retriever(h.store(), h.index(), nullptr);
    RetrievalConfig config = baseConfig();
    config.perFileLimit = 2;
    config.topK = 5;
    retriever.setConfig(config);
    retriever.setMountedCollections({collection});

    QString error;
    const QList<RetrievedChunk> chunks = retriever.retrieve(QStringLiteral("巡检记录"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chunks.size(), 2);  // 每文件上限生效，不让单个大文件占满 Top-K

    config.perFileLimit = 4;
    retriever.setConfig(config);
    QCOMPARE(retriever.retrieve(QStringLiteral("巡检记录"), 5, &error).size(), 4);
}

void TstRagRetriever::neighborExpansionAddsAdjacentChunks()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("案例库"));
    const qint64 file = h.addFile(QStringLiteral("案例.md"), collection);
    h.addChunks(file, {QStringLiteral("记录一：正常"), QStringLiteral("记录二：压力异常"),
                       QStringLiteral("记录三：已更换密封圈"), QStringLiteral("记录四：正常"),
                       QStringLiteral("记录五：正常")});

    RagRetriever retriever(h.store(), h.index(), nullptr);
    RetrievalConfig config = baseConfig();
    config.perFileLimit = 1;
    config.neighborRadius = 1;
    retriever.setConfig(config);
    retriever.setMountedCollections({collection});

    QString error;
    const QList<RetrievedChunk> chunks = retriever.retrieve(QStringLiteral("压力异常"), 3, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chunks.size(), 3);  // 命中 1 条 + 前后邻居各 1 条

    QSet<int> ordinals;
    for (const RetrievedChunk &chunk : chunks) {
        ordinals.insert(chunk.ordinal);
    }
    // ordinal 从 0 起：命中「记录二」（ordinal 1），前后邻居为 0 与 2
    QCOMPARE(ordinals, (QSet<int>{0, 1, 2}));
    bool hitFound = false;
    for (const RetrievedChunk &chunk : chunks) {
        QCOMPARE(chunk.fileName, QStringLiteral("案例.md"));
        QVERIFY(!chunk.locLabel.isEmpty());
        if (chunk.ordinal == 1) {
            hitFound = true;
            QVERIFY(chunk.text.contains(QStringLiteral("压力异常")));
        }
    }
    QVERIFY(hitFound);
}

void TstRagRetriever::tokenBudgetLimitsContext()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("预算库"));
    const qint64 file = h.addFile(QStringLiteral("预算.md"), collection);
    const QString body = QString(120, QChar(0x5DE1));  // 「巡」×120 ≈ 120 token
    h.addChunks(file, {body, body, body, body});

    RagRetriever retriever(h.store(), h.index(), nullptr);
    RetrievalConfig config = baseConfig();
    config.perFileLimit = 4;
    config.topK = 4;
    config.tokenBudget = 250;
    retriever.setConfig(config);
    retriever.setMountedCollections({collection});

    QString error;
    const QList<RetrievedChunk> chunks = retriever.retrieve(QStringLiteral("巡"), 4, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(chunks.size() <= 2, "token 预算未生效");

    config.tokenBudget = 5000;
    retriever.setConfig(config);
    QCOMPARE(retriever.retrieve(QStringLiteral("巡"), 4, &error).size(), 4);
}

void TstRagRetriever::collectionFilterKeepsOnlyMounted()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 first = h.addCollection(QStringLiteral("库一"));
    const qint64 second = h.addCollection(QStringLiteral("库二"));
    const qint64 fileA = h.addFile(QStringLiteral("A.md"), first);
    const qint64 fileB = h.addFile(QStringLiteral("B.md"), second);
    h.addChunks(fileA, {QStringLiteral("消防泵房巡检 A")});
    h.addChunks(fileB, {QStringLiteral("消防泵房巡检 B")});

    RagRetriever retriever(h.store(), h.index(), nullptr);
    retriever.setConfig(baseConfig());
    retriever.setMountedCollections({first});

    QString error;
    const QList<RetrievedChunk> chunks = retriever.retrieve(QStringLiteral("消防泵房"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.first().fileName, QStringLiteral("A.md"));
}

void TstRagRetriever::emptyMountReturnsNothing()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("未挂载库"));
    const qint64 file = h.addFile(QStringLiteral("X.md"), collection);
    h.addChunks(file, {QStringLiteral("消防泵房巡检")});

    RagRetriever retriever(h.store(), h.index(), nullptr);
    retriever.setConfig(baseConfig());

    bool done = false;
    RetrievalResult result;
    connect(&retriever, &RagRetriever::finished, this, [&](const RetrievalResult &value) {
        done = true;
        result = value;
    });
    retriever.retrieveAsync(QStringLiteral("消防泵房"));
    QVERIFY(done);
    QVERIFY(result.chunks.isEmpty());
    QVERIFY(result.note.contains(QStringLiteral("未挂载")));
}

void TstRagRetriever::vectorSearchAppliesThreshold()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("向量库"));
    const qint64 file = h.addFile(QStringLiteral("向量.md"), collection);
    const QList<qint64> chunkIds =
        h.addChunks(file, {QStringLiteral("高相似片段"), QStringLiteral("低相似片段")});

    QString error;
    QList<StoredVector> vectors;
    StoredVector high;
    high.chunkId = chunkIds.at(0);
    high.values = QVector<float>{1.0f, 0.0f};
    StoredVector low;
    low.chunkId = chunkIds.at(1);
    low.values = QVector<float>{0.0f, 1.0f};
    vectors << high << low;
    QVERIFY2(h.index()->upsert(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"),
                               vectors, &error),
             qPrintable(error));

    FakeNetworkTransport transport;
    EmbeddingService embedding(&transport);
    embedding.configure(providerConfig(), embeddingConfig());

    RagRetriever retriever(h.store(), h.index(), &embedding);
    RetrievalConfig config = baseConfig();
    config.ftsFallback = false;  // 只看纯向量路径
    retriever.setConfig(config);
    retriever.setMountedCollections({collection});
    retriever.setEmbeddingModel(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"));

    bool done = false;
    RetrievalResult result;
    connect(&retriever, &RagRetriever::finished, this, [&](const RetrievalResult &value) {
        done = true;
        result = value;
    });

    retriever.retrieveAsync(QStringLiteral("任意查询"));
    QCOMPARE(transport.m_requests.size(), 1);
    transport.pushChunk(transport.m_lastHandle, embedResponse(QVector<float>{1.0f, 0.0f}));
    transport.pushFinished(transport.m_lastHandle);

    QVERIFY(done);
    QVERIFY(!result.degraded);
    QCOMPARE(result.mode, QStringLiteral("vector"));
    QCOMPARE(result.vectorHits, 1);  // 低相似度（cos = 0）被阈值过滤
    QCOMPARE(result.chunks.size(), 1);
    QCOMPARE(result.chunks.first().text, QStringLiteral("高相似片段"));
    QVERIFY(result.chunks.first().score > 0.9);
}

void TstRagRetriever::embeddingFailureFallsBackToKeyword()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("降级库"));
    const qint64 file = h.addFile(QStringLiteral("降级.md"), collection);
    h.addChunks(file, {QStringLiteral("消防泵房巡检要求")});

    FakeNetworkTransport transport;
    EmbeddingService embedding(&transport);
    embedding.configure(providerConfig(), embeddingConfig());

    RagRetriever retriever(h.store(), h.index(), &embedding);
    retriever.setConfig(baseConfig());
    retriever.setMountedCollections({collection});
    retriever.setEmbeddingModel(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"));

    bool done = false;
    RetrievalResult result;
    connect(&retriever, &RagRetriever::finished, this, [&](const RetrievalResult &value) {
        done = true;
        result = value;
    });

    retriever.retrieveAsync(QStringLiteral("消防泵房"));
    transport.pushFailed(transport.m_lastHandle, 429, NetworkTransport::Failure::Other,
                         QStringLiteral("rate limited"));
    QVERIFY(done);
    QVERIFY(result.degraded);
    QCOMPARE(result.mode, QStringLiteral("keyword"));
    QVERIFY(result.note.contains(QStringLiteral("降级")));
    QCOMPARE(result.chunks.size(), 1);
}

void TstRagRetriever::syncRetrieveNeverTouchesNetwork()
{
    Harness h;
    QVERIFY2(h.error().isEmpty(), qPrintable(h.error()));
    const qint64 collection = h.addCollection(QStringLiteral("同步库"));
    const qint64 file = h.addFile(QStringLiteral("同步.md"), collection);
    h.addChunks(file, {QStringLiteral("消防泵房巡检")});

    FakeNetworkTransport transport;
    EmbeddingService embedding(&transport);
    embedding.configure(providerConfig(), embeddingConfig());

    RagRetriever retriever(h.store(), h.index(), &embedding);
    retriever.setConfig(baseConfig());
    retriever.setMountedCollections({collection});
    retriever.setEmbeddingModel(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"));

    QString error;
    const QList<RetrievedChunk> chunks = retriever.retrieve(QStringLiteral("消防泵房"), 3, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(transport.m_requests.size(), 0);  // 同步接口绝不联网
}

QTEST_GUILESS_MAIN(TstRagRetriever)
#include "tst_ragretriever.moc"
