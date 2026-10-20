#include "core/ai/ProviderCatalog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(data);
    file.close();
    return true;
}

/// 构造一条厂商条目；models 为 JSON 文本片段（便于书写非法/漂移用例）
QJsonObject providerEntry(const QString &id, const QString &protocol, const QString &baseUrl,
                          const QString &authStyle, const QJsonArray &models)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), id);
    entry.insert(QStringLiteral("displayName"), id);
    entry.insert(QStringLiteral("protocol"), protocol);
    entry.insert(QStringLiteral("baseUrl"), baseUrl);
    entry.insert(QStringLiteral("authStyle"), authStyle);
    entry.insert(QStringLiteral("models"), models);
    return entry;
}

QJsonObject modelEntry(const QString &id, const QStringList &levels, const QJsonObject &native)
{
    QJsonObject thinking;
    thinking.insert(QStringLiteral("levels"), QJsonArray::fromStringList(levels));
    thinking.insert(QStringLiteral("native"), native);

    QJsonObject model;
    model.insert(QStringLiteral("id"), id);
    model.insert(QStringLiteral("displayName"), id);
    model.insert(QStringLiteral("supportsVision"), false);
    model.insert(QStringLiteral("isDefault"), true);
    model.insert(QStringLiteral("thinkingSpec"), thinking);
    return model;
}

QByteArray catalogJson(const QJsonArray &providers)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("providers"), providers);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

/// 把目录写入临时目录并返回文件路径
QString writeCatalog(const QTemporaryDir &dir, const QJsonArray &providers)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("providers.json"));
    return writeFile(path, catalogJson(providers)) ? path : QString();
}

}  // namespace

/**
 * 厂商目录（数据层）测试：
 *  - 文件缺失 / JSON 损坏 / 空目录 → 明确失败且不崩溃；
 *  - 解析厂商与模型、默认模型、QML 视图数据；
 *  - 档位与原生映射的**契约漂移**检测；
 *  - 未知厂商 id 退化为「自定义」逃生口；
 *  - **内置目录自检**：resources/providers.json 必须自洽且不含豆包 / 火山方舟。
 */
class TstProviderCatalog : public QObject
{
    Q_OBJECT

private slots:
    void missingFileFailsSoftly();
    void brokenJsonFails();
    void emptyProviderListFails();
    void parsesProvidersAndModels();
    void thinkingLevelDriftIsReported();
    void thinkingNativeDriftDropsModel();
    void unknownProviderFallsBackToCustomEntry();
    void nativeSearchIsParsedAndIgnoredWhenInvalid();
    void shippedCatalogIsConsistent();
};

void TstProviderCatalog::missingFileFailsSoftly()
{
    ProviderCatalog catalog(QDir(QDir::tempPath()).filePath(QStringLiteral("no-such-catalog.json")));
    QString error;
    QVERIFY(!catalog.load(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!catalog.available());
    QVERIFY(catalog.providerIds().isEmpty());

    // 未加载时查询也必须安全返回
    QVERIFY(!catalog.provider(QStringLiteral("openai")).isValid());
    QVERIFY(catalog.modelOptions(QStringLiteral("openai")).isEmpty());
    QVERIFY(catalog.modelInfo(QStringLiteral("openai"), QStringLiteral("x")).isEmpty());
}

void TstProviderCatalog::brokenJsonFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("providers.json"));
    QVERIFY(writeFile(path, QByteArrayLiteral("{ not json")));

    ProviderCatalog catalog(path);
    QString error;
    QVERIFY(!catalog.load(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!catalog.available());
}

void TstProviderCatalog::emptyProviderListFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeCatalog(dir, QJsonArray());
    QVERIFY(!path.isEmpty());

    ProviderCatalog catalog(path);
    QString error;
    QVERIFY(!catalog.load(&error));
    QVERIFY(!error.isEmpty());
}

void TstProviderCatalog::parsesProvidersAndModels()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QJsonObject native{
        {QStringLiteral("high"), QJsonObject{{QStringLiteral("reasoning_effort"),
                                              QStringLiteral("high")}}}};

    QJsonObject entry = providerEntry(QStringLiteral("v1"), QStringLiteral("openai"),
                                      QStringLiteral("https://example.test/v1"),
                                      QStringLiteral("bearer"),
                                      QJsonArray{modelEntry(QStringLiteral("m1"),
                                                            QStringList{QStringLiteral("high")},
                                                            native)});
    entry.insert(QStringLiteral("extraHeaders"),
                 QJsonObject{{QStringLiteral("x-extra"), QStringLiteral("1")}});
    entry.insert(QStringLiteral("validation"),
                 QJsonObject{{QStringLiteral("method"), QStringLiteral("models")}});

    const QString path = writeCatalog(dir, QJsonArray{entry});
    QVERIFY(!path.isEmpty());

    ProviderCatalog catalog(path);
    QString error;
    QVERIFY2(catalog.load(&error), qPrintable(error));
    QVERIFY(catalog.available());
    QCOMPARE(catalog.providerIds(), QStringList{QStringLiteral("v1")});

    const CatalogProvider provider = catalog.provider(QStringLiteral("v1"));
    QVERIFY(provider.isValid());
    QCOMPARE(provider.protocol, QStringLiteral("openai"));
    QCOMPARE(provider.baseUrl, QStringLiteral("https://example.test/v1"));
    QCOMPARE(provider.authStyle, QStringLiteral("bearer"));
    QCOMPARE(provider.validationMethod, QStringLiteral("models"));
    QCOMPARE(provider.extraHeaders.value(QStringLiteral("x-extra")).toString(),
             QStringLiteral("1"));
    QCOMPARE(provider.defaultModelId(), QStringLiteral("m1"));

    const CatalogModel model = provider.model(QStringLiteral("m1"));
    QVERIFY(model.isValid());
    QCOMPARE(model.thinking.levels, QStringList{QStringLiteral("high")});
    QCOMPARE(model.thinking.nativeFor(QStringLiteral("high"))
                 .value(QStringLiteral("reasoning_effort"))
                 .toString(),
             QStringLiteral("high"));
    // 未声明的档位不注入任何字段
    QVERIFY(model.thinking.nativeFor(QStringLiteral("low")).isEmpty());

    // QML 视图：UI 只消费这些数据
    const QVariantList options = catalog.modelOptions(QStringLiteral("v1"));
    QCOMPARE(options.size(), 1);
    QCOMPARE(options.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("m1"));
    QVERIFY(options.first().toMap().value(QStringLiteral("supportsThinking")).toBool());
}

void TstProviderCatalog::thinkingLevelDriftIsReported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 未知档位「max」：丢弃该档位，模型仍可用（不因一处笔误让整条目录失效）
    const QJsonObject native{{QStringLiteral("high"), QJsonObject{}}};
    const QString path =
        writeCatalog(dir,
                     QJsonArray{providerEntry(
                         QStringLiteral("v1"), QStringLiteral("openai"),
                         QStringLiteral("https://example.test/v1"), QStringLiteral("bearer"),
                         QJsonArray{modelEntry(QStringLiteral("m1"),
                                               QStringList{QStringLiteral("high"),
                                                           QStringLiteral("max")},
                                               native)})});
    QVERIFY(!path.isEmpty());

    ProviderCatalog catalog(path);
    QVERIFY(catalog.load());
    QCOMPARE(catalog.provider(QStringLiteral("v1")).model(QStringLiteral("m1")).thinking.levels,
             QStringList{QStringLiteral("high")});
    QVERIFY(!catalog.warnings().isEmpty());
}

void TstProviderCatalog::thinkingNativeDriftDropsModel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 声明了 low/high，但 native 只给 high（缺档位）→ 映射不可信，丢弃该模型
    const QJsonObject missing{{QStringLiteral("high"), QJsonObject{}}};
    // native 多出未声明档位 → 同样丢弃
    const QJsonObject extra{{QStringLiteral("high"), QJsonObject{}},
                            {QStringLiteral("off"), QJsonObject{}}};

    const QString path =
        writeCatalog(dir,
                     QJsonArray{providerEntry(
                         QStringLiteral("v1"), QStringLiteral("openai"),
                         QStringLiteral("https://example.test/v1"), QStringLiteral("bearer"),
                         QJsonArray{
                             modelEntry(QStringLiteral("dropped-missing"),
                                        QStringList{QStringLiteral("low"), QStringLiteral("high")},
                                        missing),
                             modelEntry(QStringLiteral("dropped-extra"),
                                        QStringList{QStringLiteral("high")}, extra),
                             modelEntry(QStringLiteral("kept"), QStringList{}, QJsonObject{}),
                         })});
    QVERIFY(!path.isEmpty());

    ProviderCatalog catalog(path);
    QVERIFY(catalog.load());
    const CatalogProvider provider = catalog.provider(QStringLiteral("v1"));
    QVERIFY(!provider.model(QStringLiteral("dropped-missing")).isValid());
    QVERIFY(!provider.model(QStringLiteral("dropped-extra")).isValid());
    QVERIFY(provider.model(QStringLiteral("kept")).isValid());
    QVERIFY(catalog.warnings().size() >= 2);
}

void TstProviderCatalog::unknownProviderFallsBackToCustomEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject custom;
    custom.insert(QStringLiteral("id"), QStringLiteral("custom"));
    custom.insert(QStringLiteral("protocol"), QStringLiteral("custom"));
    custom.insert(QStringLiteral("authStyle"), QStringLiteral("bearer"));
    custom.insert(QStringLiteral("customizable"), true);
    custom.insert(QStringLiteral("models"), QJsonArray());

    const QString path =
        writeCatalog(dir,
                     QJsonArray{custom,
                                providerEntry(QStringLiteral("v1"), QStringLiteral("openai"),
                                              QStringLiteral("https://example.test/v1"),
                                              QStringLiteral("bearer"),
                                              QJsonArray{modelEntry(QStringLiteral("m1"),
                                                                    QStringList{},
                                                                    QJsonObject{})})});
    QVERIFY(!path.isEmpty());

    ProviderCatalog catalog(path);
    QVERIFY(catalog.load());

    const CatalogProvider fallback = catalog.provider(QStringLiteral("does-not-exist"));
    QCOMPARE(fallback.id, QStringLiteral("custom"));
    QVERIFY(fallback.customizable);
}

void TstProviderCatalog::nativeSearchIsParsedAndIgnoredWhenInvalid()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 合法声明：可用的内置搜索
    QJsonObject valid = providerEntry(QStringLiteral("v1"), QStringLiteral("openai"),
                                     QStringLiteral("https://example.test/v1"),
                                     QStringLiteral("bearer"),
                                     QJsonArray{modelEntry(QStringLiteral("m1"), QStringList{},
                                                           QJsonObject{})});
    valid.insert(QStringLiteral("nativeSearch"),
                 QJsonObject{{QStringLiteral("protocol"), QStringLiteral("DeepSeek")},
                             {QStringLiteral("searchEndpoint"),
                              QStringLiteral("https://api.example.test/v1/tools/search")},
                             {QStringLiteral("fetchEndpoint"),
                              QStringLiteral("https://api.example.test/v1/tools/fetch")},
                             {QStringLiteral("reuseProviderKey"), true}});

    // 缺 searchEndpoint：只记警告并忽略声明，不阻断加载
    QJsonObject missing = providerEntry(QStringLiteral("v2"), QStringLiteral("openai"),
                                        QStringLiteral("https://example.test/v1"),
                                        QStringLiteral("bearer"),
                                        QJsonArray{modelEntry(QStringLiteral("m2"), QStringList{},
                                                              QJsonObject{})});
    missing.insert(QStringLiteral("nativeSearch"),
                   QJsonObject{{QStringLiteral("protocol"), QStringLiteral("deepseek")},
                               {QStringLiteral("reuseProviderKey"), true}});

    // 非 http 地址：同样忽略
    QJsonObject badUrl = providerEntry(QStringLiteral("v3"), QStringLiteral("openai"),
                                       QStringLiteral("https://example.test/v1"),
                                       QStringLiteral("bearer"),
                                       QJsonArray{modelEntry(QStringLiteral("m3"), QStringList{},
                                                             QJsonObject{})});
    badUrl.insert(QStringLiteral("nativeSearch"),
                  QJsonObject{{QStringLiteral("searchEndpoint"), QStringLiteral("ftp://x/search")}});

    const QString path = writeCatalog(dir, QJsonArray{valid, missing, badUrl});
    QVERIFY(!path.isEmpty());

    ProviderCatalog catalog(path);
    QString error;
    QVERIFY2(catalog.load(&error), qPrintable(error));

    const CatalogNativeSearch native = catalog.provider(QStringLiteral("v1")).nativeSearch;
    QVERIFY(native.isValid());
    QCOMPARE(native.protocol, QStringLiteral("deepseek"));  // 归一化为小写
    QCOMPARE(native.searchEndpoint, QStringLiteral("https://api.example.test/v1/tools/search"));
    QCOMPARE(native.fetchEndpoint, QStringLiteral("https://api.example.test/v1/tools/fetch"));
    QCOMPARE(native.reuseProviderKey, true);

    QVERIFY(!catalog.provider(QStringLiteral("v2")).nativeSearch.isValid());
    QVERIFY(!catalog.provider(QStringLiteral("v3")).nativeSearch.isValid());
    QVERIFY(!catalog.warnings().isEmpty());
    // 非法声明不影响厂商本身可用
    QVERIFY(catalog.provider(QStringLiteral("v2")).isValid());
}

void TstProviderCatalog::shippedCatalogIsConsistent()
{
    const QString sourceDir = QStringLiteral(MEOWAI_SOURCE_DIR);
    const QString path = sourceDir + QStringLiteral("/resources/providers.json");
    QVERIFY2(QFile::exists(path), qPrintable(path));

    ProviderCatalog catalog(path);
    QString error;
    QVERIFY2(catalog.load(&error), qPrintable(error));
    QVERIFY2(catalog.warnings().isEmpty(),
             qPrintable(catalog.warnings().join(QStringLiteral(" | "))));

    // 内置厂商：本次必须做到的 5 家 + Anthropic 保留 + 自定义逃生口
    const QStringList expected{QStringLiteral("openai"), QStringLiteral("deepseek"),
                              QStringLiteral("qwen"), QStringLiteral("kimi"),
                              QStringLiteral("zhipu"), QStringLiteral("anthropic"),
                              QStringLiteral("custom")};
    QCOMPARE(catalog.providerIds(), expected);

    // 明确不接入：豆包 / 火山方舟 Ark
    for (const QString &id : catalog.providerIds()) {
        QVERIFY(!id.contains(QStringLiteral("ark"), Qt::CaseInsensitive));
        QVERIFY(!id.contains(QStringLiteral("doubao"), Qt::CaseInsensitive));
    }

    const QStringList knownLevels{QStringLiteral("off"), QStringLiteral("low"),
                                 QStringLiteral("medium"), QStringLiteral("high")};
    for (const CatalogProvider &provider : catalog.providers()) {
        // 「自定义」逃生口由用户手填 baseUrl，其余厂商必须显式给出域名
        if (!provider.customizable)
            QVERIFY2(!provider.baseUrl.isEmpty(), qPrintable(provider.id));
        QVERIFY(provider.authStyle == QStringLiteral("bearer")
                || provider.authStyle == QStringLiteral("x-api-key")
                || provider.authStyle == QStringLiteral("query-key"));
        QVERIFY(provider.validationMethod == QStringLiteral("models")
                || provider.validationMethod == QStringLiteral("chat-minimal"));

        if (!provider.customizable)
            QVERIFY2(!provider.models.isEmpty(), qPrintable(provider.id));

        for (const CatalogModel &model : provider.models) {
            // 档位必须来自统一集合，且 native 与 levels 严格一一对应
            for (const QString &level : model.thinking.levels)
                QVERIFY2(knownLevels.contains(level), qPrintable(model.id + QLatin1Char('/') + level));
            const QStringList nativeKeys = model.thinking.native.keys();
            QCOMPARE(QSet<QString>(nativeKeys.begin(), nativeKeys.end()),
                     QSet<QString>(model.thinking.levels.begin(), model.thinking.levels.end()));
            // 不支持思考的模型不得携带任何原生映射
            if (!model.thinking.supportsThinking())
                QVERIFY2(model.thinking.native.isEmpty(),
                         qPrintable(QStringLiteral("非思考模型仍有原生映射：%1").arg(model.id)));
            QVERIFY(!model.displayName.isEmpty());
        }

        // 默认模型必须真实存在于该厂商的模型列表中
        const QString defaultId = provider.defaultModelId();
        if (!defaultId.isEmpty())
            QVERIFY2(provider.model(defaultId).isValid(), qPrintable(provider.id + defaultId));
    }

    // Anthropic 的鉴权头必须来自目录，而不是代码里写死
    QCOMPARE(catalog.provider(QStringLiteral("anthropic"))
                 .extraHeaders.value(QStringLiteral("anthropic-version"))
                 .toString(),
             QStringLiteral("2023-06-01"));

    // 内置联网搜索：本次仅 DeepSeek 声明；必须复用厂商 Key（零配置）
    const CatalogNativeSearch deepseekNative =
        catalog.provider(QStringLiteral("deepseek")).nativeSearch;
    QVERIFY(deepseekNative.isValid());
    QCOMPARE(deepseekNative.protocol, QStringLiteral("deepseek"));
    QCOMPARE(deepseekNative.reuseProviderKey, true);
    QVERIFY(deepseekNative.searchEndpoint.startsWith(QStringLiteral("https://")));
    QVERIFY(deepseekNative.fetchEndpoint.startsWith(QStringLiteral("https://")));

    int nativeSearchCount = 0;
    for (const CatalogProvider &provider : catalog.providers()) {
        if (provider.nativeSearch.isValid()) {
            ++nativeSearchCount;
            // 只有受支持的内置协议才允许声明，避免把未知协议误当通用端点
            QVERIFY2(provider.nativeSearch.reuseProviderKey, qPrintable(provider.id));
        }
    }
    QCOMPARE(nativeSearchCount, 1);
}

QTEST_GUILESS_MAIN(TstProviderCatalog)
#include "tst_providercatalog.moc"
