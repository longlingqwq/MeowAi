#include "storage/AiConfigStore.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

/**
 * AI 接入配置存储测试（v0.2.3）。
 * 全部离线：只操作临时目录中的 config.json，不触碰真实用户配置。
 */
class TstAiConfigStore : public QObject
{
    Q_OBJECT

private slots:
    void loadReturnsDefaultsWhenFileMissing();
    void saveAndLoadRoundTrip();
    void savePreservesOtherSections();
    void corruptJsonFallsBackWithoutCrash();
    void legacyModelSectionIsMigrated();
    void selectionKeyFormat();

private:
    QString configPath(const QTemporaryDir &dir) const
    {
        return QDir(dir.path()).filePath(QStringLiteral("config.json"));
    }
    QJsonObject readRoot(const QString &path) const
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(file.readAll()).object();
    }
};

void TstAiConfigStore::loadReturnsDefaultsWhenFileMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AiConfigStore store(configPath(dir));
    QString error;
    const AiConfigData data = store.load(&error);

    QVERIFY(error.isEmpty());  // 首次运行没有配置文件属正常情况，不算错误
    QVERIFY(data.providerId.isEmpty());
    QVERIFY(data.modelId.isEmpty());
    QVERIFY(data.thinkingLevels.isEmpty());
    QVERIFY(data.customBaseUrl.isEmpty());
}

void TstAiConfigStore::saveAndLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AiConfigStore store(configPath(dir));
    AiConfigData data;
    data.providerId = QStringLiteral("deepseek");
    data.modelId = QStringLiteral("deepseek-v4-pro");
    data.customBaseUrl = QStringLiteral("https://my.gateway.example/v1");
    data.thinkingLevels.insert(QStringLiteral("deepseek.deepseek-v4-pro"), QStringLiteral("off"));
    data.nativeOverrides.insert(QStringLiteral("openai.gpt-5"),
                                QJsonObject{{QStringLiteral("reasoning_effort"), QStringLiteral("low")}});

    QString error;
    QVERIFY(store.save(data, &error));
    QVERIFY(error.isEmpty());

    const AiConfigData loaded = store.load(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.providerId, data.providerId);
    QCOMPARE(loaded.modelId, data.modelId);
    QCOMPARE(loaded.customBaseUrl, data.customBaseUrl);
    QCOMPARE(loaded.thinkingLevels.value(QStringLiteral("deepseek.deepseek-v4-pro")),
             QStringLiteral("off"));
    QCOMPARE(loaded.nativeOverrides.value(QStringLiteral("openai.gpt-5"))
                 .value(QStringLiteral("reasoning_effort"))
                 .toString(),
             QStringLiteral("low"));
}

void TstAiConfigStore::savePreservesOtherSections()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = configPath(dir);

    // 预先写入其他模块的段，验证 ai 段写入不会覆盖它们
    QJsonObject other;
    other.insert(QStringLiteral("theme"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("theme2")}});
    other.insert(QStringLiteral("personalization"),
                 QJsonObject{{QStringLiteral("font_family"), QStringLiteral("Inter")}});
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(other).toJson());
    file.close();

    AiConfigStore store(path);
    AiConfigData data;
    data.providerId = QStringLiteral("kimi");
    data.modelId = QStringLiteral("kimi-k3");
    QString error;
    QVERIFY(store.save(data, &error));

    const QJsonObject root = readRoot(path);
    QCOMPARE(root.value(QStringLiteral("theme"))
                 .toObject()
                 .value(QStringLiteral("preset"))
                 .toString(),
             QStringLiteral("theme2"));
    QCOMPARE(root.value(QStringLiteral("personalization"))
                 .toObject()
                 .value(QStringLiteral("font_family"))
                 .toString(),
             QStringLiteral("Inter"));
    QCOMPARE(root.value(QStringLiteral("ai"))
                 .toObject()
                 .value(QStringLiteral("providerId"))
                 .toString(),
             QStringLiteral("kimi"));
}

void TstAiConfigStore::corruptJsonFallsBackWithoutCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = configPath(dir);

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json ]");
    file.close();

    AiConfigStore store(path);
    QString error;
    const AiConfigData data = store.load(&error);
    QVERIFY(!error.isEmpty());  // 明确告知损坏
    QVERIFY(data.providerId.isEmpty());  // 但回退默认，不崩溃

    // 在损坏文件上保存时以空对象重建，不追加垃圾
    AiConfigData fresh;
    fresh.providerId = QStringLiteral("openai");
    QVERIFY(store.save(fresh, &error));
    QCOMPARE(readRoot(path).value(QStringLiteral("ai")).toObject().value(QStringLiteral("providerId")).toString(),
             QStringLiteral("openai"));
}

void TstAiConfigStore::legacyModelSectionIsMigrated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = configPath(dir);

    // 阶段二遗留的扁平键：model.provider / model.name
    QJsonObject legacy;
    legacy.insert(QStringLiteral("model"),
                  QJsonObject{{QStringLiteral("provider"), QStringLiteral("anthropic")},
                              {QStringLiteral("name"), QStringLiteral("claude-sonnet-5")}});
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(legacy).toJson());
    file.close();

    AiConfigStore store(path);
    QString error;
    const AiConfigData data = store.load(&error);
    QCOMPARE(data.providerId, QStringLiteral("anthropic"));
    QCOMPARE(data.modelId, QStringLiteral("claude-sonnet-5"));
}

void TstAiConfigStore::selectionKeyFormat()
{
    QCOMPARE(AiConfigStore::selectionKey(QStringLiteral("openai"), QStringLiteral("gpt-5")),
             QStringLiteral("openai.gpt-5"));
    // 不同厂商的同名模型不得撞键
    QVERIFY(AiConfigStore::selectionKey(QStringLiteral("a"), QStringLiteral("m"))
            != AiConfigStore::selectionKey(QStringLiteral("b"), QStringLiteral("m")));
}

QTEST_GUILESS_MAIN(TstAiConfigStore)
#include "tst_aiconfigstore.moc"
