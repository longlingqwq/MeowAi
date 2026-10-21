#include "storage/LibraryConfigStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 资料库配置存储测试：
 *  - 默认值（首次启用前）与读写往返；
 *  - **只动 `library` 段**：ai / theme / personalization / web 等其他段必须原样保留；
 *  - 异常流（文件缺失、内容损坏、路径为空、脏 JSON 类型）不崩溃、不误判。
 */
class TstLibraryConfig : public QObject
{
    Q_OBJECT

private slots:
    void defaultsWhenFileMissing();
    void roundTripPersistsAllFields();
    void otherSectionsArePreserved();
    void corruptedFileFallsBackAndRebuilds();
    void emptyPathSaveFails();
    void invalidValuesFallBackToDefaults();

private:
    static QString configPath(const QTemporaryDir &tmp)
    {
        return QDir(tmp.path()).filePath(QStringLiteral("config.json"));
    }

    static QJsonObject readRoot(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject();
        const QByteArray data = file.readAll();
        file.close();
        const QJsonDocument document = QJsonDocument::fromJson(data);
        return document.isObject() ? document.object() : QJsonObject();
    }

    static bool writeText(const QString &path, const QByteArray &content)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(content);
        file.close();
        return true;
    }
};

void TstLibraryConfig::defaultsWhenFileMissing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const LibraryConfigStore store(configPath(tmp));
    QString error;
    const LibraryConfigData data = store.load(&error);

    // 配置文件不存在不算错误：以默认值继续
    QVERIFY(error.isEmpty());
    QVERIFY(!data.enabled);
    QVERIFY(!data.consentGiven);
    QCOMPARE(data.consentAt, qint64(0));
    QCOMPARE(data.topK, 6);
    QCOMPARE(data.perFileLimit, 2);
    QCOMPARE(data.tokenBudget, 2000);
    QCOMPARE(data.similarityThreshold, 0.30);
    QCOMPARE(data.neighborRadius, 1);
    QVERIFY(data.ftsFallback);
    QCOMPARE(data.chunkSize, 700);
    QCOMPARE(data.chunkOverlap, 120);
    QCOMPARE(data.embedding.batchSize, 16);
    QCOMPARE(data.caption.maxEdge, 1536);
    QCOMPARE(data.caption.maxTokens, 500);
    QCOMPARE(data.caption.promptProfile, QStringLiteral("default"));
    QVERIFY(data.mountedCollections.isEmpty());
}

void TstLibraryConfig::roundTripPersistsAllFields()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const LibraryConfigStore store(configPath(tmp));

    LibraryConfigData data;
    data.enabled = true;
    data.consentGiven = true;
    data.consentAt = 1700000000000;
    data.topK = 9;
    data.perFileLimit = 3;
    data.tokenBudget = 4096;
    data.similarityThreshold = 0.42;
    data.neighborRadius = 2;
    data.ftsFallback = false;
    data.chunkSize = 900;
    data.chunkOverlap = 150;
    data.embedding.providerId = QStringLiteral("qwen");
    data.embedding.modelId = QStringLiteral("text-embedding-v4");
    data.embedding.dimensions = 1024;
    data.embedding.batchSize = 10;
    data.caption.providerId = QStringLiteral("zhipu");
    data.caption.modelId = QStringLiteral("glm-4v-plus-0111");
    data.caption.maxEdge = 1024;
    data.caption.maxTokens = 400;
    data.caption.promptProfile = QStringLiteral("text-heavy");
    data.mountedCollections = { 3, 7 };

    QString error;
    QVERIFY2(store.save(data, &error), qPrintable(error));

    const LibraryConfigData loaded = store.load(&error);
    QVERIFY(error.isEmpty());
    QVERIFY(loaded.enabled);
    QVERIFY(loaded.consentGiven);
    QCOMPARE(loaded.consentAt, qint64(1700000000000));
    QCOMPARE(loaded.topK, 9);
    QCOMPARE(loaded.perFileLimit, 3);
    QCOMPARE(loaded.tokenBudget, 4096);
    QCOMPARE(loaded.similarityThreshold, 0.42);
    QCOMPARE(loaded.neighborRadius, 2);
    QVERIFY(!loaded.ftsFallback);
    QCOMPARE(loaded.chunkSize, 900);
    QCOMPARE(loaded.chunkOverlap, 150);
    QCOMPARE(loaded.embedding.providerId, QStringLiteral("qwen"));
    QCOMPARE(loaded.embedding.modelId, QStringLiteral("text-embedding-v4"));
    QCOMPARE(loaded.embedding.dimensions, 1024);
    QCOMPARE(loaded.embedding.batchSize, 10);
    QCOMPARE(loaded.caption.providerId, QStringLiteral("zhipu"));
    QCOMPARE(loaded.caption.modelId, QStringLiteral("glm-4v-plus-0111"));
    QCOMPARE(loaded.caption.maxEdge, 1024);
    QCOMPARE(loaded.caption.maxTokens, 400);
    QCOMPARE(loaded.caption.promptProfile, QStringLiteral("text-heavy"));
    QCOMPARE(loaded.mountedCollections, QList<qint64>({ 3, 7 }));
}

void TstLibraryConfig::otherSectionsArePreserved()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = configPath(tmp);

    // 其他模块的段：以对象方式构造，避免手工拼 JSON 出错
    QJsonObject existing;
    existing.insert(QStringLiteral("ai"),
                    QJsonObject({ { QStringLiteral("providerId"), QStringLiteral("deepseek") },
                                  { QStringLiteral("modelId"), QStringLiteral("deepseek-v4-pro") } }));
    existing.insert(QStringLiteral("theme"),
                    QJsonObject({ { QStringLiteral("preset"), QStringLiteral("theme2") } }));
    existing.insert(QStringLiteral("personalization"),
                    QJsonObject({ { QStringLiteral("nickname"), QStringLiteral("阿喵") } }));
    existing.insert(QStringLiteral("web"),
                    QJsonObject({ { QStringLiteral("enabled"), true },
                                  { QStringLiteral("endpoint"), QStringLiteral("https://example.com/s") } }));
    QVERIFY(writeText(path, QJsonDocument(existing).toJson()));

    const LibraryConfigStore store(path);
    LibraryConfigData data;
    QString error;
    data = store.load(&error);

    // 已有 library 段缺失 → 默认值，且其他段不受影响
    QVERIFY(!data.enabled);

    data.enabled = true;
    data.topK = 8;
    QVERIFY2(store.save(data, &error), qPrintable(error));

    const QJsonObject root = readRoot(path);
    QCOMPARE(root.value(QStringLiteral("ai")).toObject().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("deepseek"));
    QCOMPARE(root.value(QStringLiteral("theme")).toObject().value(QStringLiteral("preset")).toString(),
             QStringLiteral("theme2"));
    QCOMPARE(root.value(QStringLiteral("personalization")).toObject().value(QStringLiteral("nickname")).toString(),
             QStringLiteral("阿喵"));
    QCOMPARE(root.value(QStringLiteral("web")).toObject().value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("https://example.com/s"));
    QCOMPARE(root.value(QStringLiteral("library")).toObject().value(QStringLiteral("topK")).toInt(), 8);

    // 再存一次：仍不能破坏其他段
    data.topK = 4;
    QVERIFY(store.save(data, &error));
    QCOMPARE(readRoot(path).value(QStringLiteral("ai")).toObject().value(QStringLiteral("modelId")).toString(),
             QStringLiteral("deepseek-v4-pro"));
}

void TstLibraryConfig::corruptedFileFallsBackAndRebuilds()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = configPath(tmp);
    QVERIFY(writeText(path, "{ this is not json"));

    const LibraryConfigStore store(path);
    QString error;
    const LibraryConfigData data = store.load(&error);

    // 损坏文件：回退默认 + 明确错误提示，但不视为致命错误
    QVERIFY(!error.isEmpty());
    QCOMPARE(data.topK, 6);

    // 保存时以空对象重建，文件恢复为合法 JSON
    error.clear();
    LibraryConfigData fresh;
    fresh.enabled = true;
    QVERIFY2(store.save(fresh, &error), qPrintable(error));

    const QJsonObject root = readRoot(path);
    QVERIFY(!root.isEmpty());
    QVERIFY(root.value(QStringLiteral("library")).toObject().value(QStringLiteral("enabled")).toBool());
}

void TstLibraryConfig::emptyPathSaveFails()
{
    const LibraryConfigStore store{QString()};
    QString error;
    QVERIFY(!store.save(LibraryConfigData(), &error));
    QVERIFY(!error.isEmpty());
}

void TstLibraryConfig::invalidValuesFallBackToDefaults()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = configPath(tmp);
    // 手工编辑后的脏 JSON：类型错误 + 非法挂载库 id
    QJsonObject library;
    library.insert(QStringLiteral("enabled"), QStringLiteral("yes"));
    library.insert(QStringLiteral("topK"), QStringLiteral("many"));
    library.insert(QStringLiteral("similarityThreshold"), QStringLiteral("high"));
    library.insert(QStringLiteral("chunkOverlap"), 80);
    QJsonArray mounted;
    mounted.append(1);
    mounted.append(1);
    mounted.append(0);
    mounted.append(-5);
    mounted.append(QStringLiteral("x"));
    mounted.append(3);
    library.insert(QStringLiteral("mountedCollections"), mounted);
    QJsonObject dirty;
    dirty.insert(QStringLiteral("library"), library);
    QVERIFY(writeText(path, QJsonDocument(dirty).toJson()));

    const LibraryConfigStore store(path);
    QString error;
    const LibraryConfigData data = store.load(&error);

    QVERIFY(!data.enabled);              // 字符串不当作 true
    QCOMPARE(data.topK, 6);              // 类型不符回退默认
    QCOMPARE(data.perFileLimit, 2);
    QCOMPARE(data.similarityThreshold, 0.30);
    QCOMPARE(data.chunkOverlap, 80);     // 合法值照常生效
    QCOMPARE(data.mountedCollections, QList<qint64>({ 1, 3 }));  // 去重并丢弃非正数
}

QTEST_GUILESS_MAIN(TstLibraryConfig)
#include "tst_libraryconfig.moc"
