#include "core/ai/ProviderCatalog.h"
#include "core/library/LibraryController.h"
#include "core/retriever/EmbeddingService.h"
#include "storage/AiConfigStore.h"
#include "storage/AppPaths.h"
#include "storage/LibraryConfigStore.h"
#include "fakes/FakeNetworkTransport.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#ifndef MEOWAI_SOURCE_DIR
#define MEOWAI_SOURCE_DIR "."
#endif

namespace {
QString catalogPath()
{
    return QStringLiteral(MEOWAI_SOURCE_DIR) + QStringLiteral("/resources/providers.json");
}

struct Env
{
    QTemporaryDir tmp;
    AppPaths paths{tmp.path()};
    LibraryConfigStore libraryConfig{paths.configFile()};
    AiConfigStore aiConfig{paths.configFile()};
    ProviderCatalog catalog{catalogPath()};
    FakeNetworkTransport transport;
    EmbeddingService embedding{&transport};
    std::unique_ptr<LibraryController> library;

    bool init(QString *error)
    {
        if (!tmp.isValid() || !paths.ensureLayout(error))
            return false;
        if (!catalog.load(error))
            return false;

        library = std::make_unique<LibraryController>(
            paths, nullptr, &libraryConfig, &embedding, nullptr, nullptr, &catalog, nullptr,
            &aiConfig, nullptr);
        return true;
    }

    bool saveAi(const QString &providerId, const QString &customBaseUrl, QString *error)
    {
        AiConfigData data = aiConfig.load();
        data.providerId = providerId;
        data.customBaseUrl = customBaseUrl;
        return aiConfig.save(data, error);
    }
};
}  // namespace

class TstLibraryUrls : public QObject
{
    Q_OBJECT

private slots:
    void serviceBaseUrlHasHighestPriority();
    void sameVendorFallsBackToConversationBaseUrl();
    void differentVendorDoesNotReuseConversationBaseUrl();
    void catalogBaseUrlIsLastFallback();
    void embeddingAndCaptionKeepSeparateUrls();
    void omittedBaseUrlPreservesExistingValue();
    void clearingBaseUrlRestoresFallback();
};

void TstLibraryUrls::serviceBaseUrlHasHighestPriority()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));
    QVERIFY(env.saveAi(QStringLiteral("custom"),
                       QStringLiteral("https://talk.example.com/v1"), &error));

    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("custom"),
                                             QStringLiteral("text-embedding-test"), 4,
                                             QStringLiteral("https://embed.example.com/v1")));

    QCOMPARE(env.embedding.providerConfig().baseUrl,
             QStringLiteral("https://embed.example.com/v1"));
    QCOMPARE(env.library->effectiveEmbeddingBaseUrl(),
             QStringLiteral("https://embed.example.com/v1"));
}

void TstLibraryUrls::sameVendorFallsBackToConversationBaseUrl()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));
    QVERIFY(env.saveAi(QStringLiteral("custom"),
                       QStringLiteral("https://talk.example.com/v1"), &error));

    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("custom"),
                                             QStringLiteral("text-embedding-test"), 4));

    QCOMPARE(env.library->effectiveEmbeddingBaseUrl(),
             QStringLiteral("https://talk.example.com/v1"));
}

void TstLibraryUrls::differentVendorDoesNotReuseConversationBaseUrl()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));
    QVERIFY(env.saveAi(QStringLiteral("custom"),
                       QStringLiteral("https://talk.example.com/v1"), &error));

    // custom 的 catalog baseUrl 为空；对话厂商不同，不得串用
    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("mock"),
                                             QStringLiteral("text-embedding-test"), 4));
    QVERIFY(env.library->effectiveEmbeddingBaseUrl().isEmpty());
}

void TstLibraryUrls::catalogBaseUrlIsLastFallback()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));
    QVERIFY(env.saveAi(QStringLiteral("custom"),
                       QStringLiteral("https://talk.example.com/v1"), &error));

    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("qwen"),
                                             QStringLiteral("text-embedding-v4"), 1024));

    const QString expected = env.catalog.provider(QStringLiteral("qwen")).baseUrl;
    QVERIFY(!expected.isEmpty());
    QCOMPARE(env.library->effectiveEmbeddingBaseUrl(), expected);
}

void TstLibraryUrls::embeddingAndCaptionKeepSeparateUrls()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));
    QVERIFY(env.saveAi(QStringLiteral("custom"),
                       QStringLiteral("https://talk.example.com/v1"), &error));

    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("custom"),
                                             QStringLiteral("text-embedding-test"), 4,
                                             QStringLiteral("https://embed.example.com/v1")));
    QVERIFY(env.library->saveCaptionConfig(QStringLiteral("custom"),
                                           QStringLiteral("vision-test"),
                                           QStringLiteral("https://vision.example.com/v1")));

    QCOMPARE(env.library->effectiveEmbeddingBaseUrl(),
             QStringLiteral("https://embed.example.com/v1"));
    QCOMPARE(env.library->effectiveCaptionBaseUrl(),
             QStringLiteral("https://vision.example.com/v1"));
    QVERIFY(env.library->effectiveEmbeddingBaseUrl()
            != env.library->effectiveCaptionBaseUrl());
}

void TstLibraryUrls::omittedBaseUrlPreservesExistingValue()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));

    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("custom"),
                                             QStringLiteral("text-embedding-test"), 4,
                                             QStringLiteral("https://embed.example.com/v1")));
    // 3 参旧调用：不得清掉已保存的自定义地址
    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("custom"),
                                             QStringLiteral("text-embedding-test"), 4));
    QCOMPARE(env.library->effectiveEmbeddingBaseUrl(),
             QStringLiteral("https://embed.example.com/v1"));

    QVERIFY(env.library->saveCaptionConfig(QStringLiteral("custom"),
                                           QStringLiteral("vision-test"),
                                           QStringLiteral("https://vision.example.com/v1")));
    QVERIFY(env.library->saveCaptionConfig(QStringLiteral("custom"),
                                           QStringLiteral("vision-test")));
    QCOMPARE(env.library->effectiveCaptionBaseUrl(),
             QStringLiteral("https://vision.example.com/v1"));
}

void TstLibraryUrls::clearingBaseUrlRestoresFallback()
{
    Env env;
    QString error;
    QVERIFY2(env.init(&error), qPrintable(error));
    QVERIFY(env.saveAi(QStringLiteral("custom"),
                       QStringLiteral("https://talk.example.com/v1"), &error));

    QVERIFY(env.library->saveEmbeddingConfig(QStringLiteral("custom"),
                                             QStringLiteral("text-embedding-test"), 4,
                                             QStringLiteral("https://embed.example.com/v1")));
    QVERIFY(env.library->clearEmbeddingBaseUrl());
    QCOMPARE(env.library->effectiveEmbeddingBaseUrl(),
             QStringLiteral("https://talk.example.com/v1"));

    QVERIFY(env.library->saveCaptionConfig(QStringLiteral("custom"),
                                           QStringLiteral("vision-test"),
                                           QStringLiteral("https://vision.example.com/v1")));
    QVERIFY(env.library->clearCaptionBaseUrl());
    QCOMPARE(env.library->effectiveCaptionBaseUrl(),
             QStringLiteral("https://talk.example.com/v1"));
}

QTEST_GUILESS_MAIN(TstLibraryUrls)
#include "tst_libraryurls.moc"
