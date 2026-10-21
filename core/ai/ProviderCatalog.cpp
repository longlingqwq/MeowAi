#include "core/ai/ProviderCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

/// 统一抽象档位：off / low / medium / high（厂商支持范围由 catalog 声明）
const QStringList &knownLevels()
{
    static const QStringList levels{QStringLiteral("off"), QStringLiteral("low"),
                                    QStringLiteral("medium"), QStringLiteral("high")};
    return levels;
}

const QStringList &knownProtocols()
{
    static const QStringList protocols{QStringLiteral("openai"), QStringLiteral("anthropic"),
                                       QStringLiteral("custom")};
    return protocols;
}

const QStringList &knownAuthStyles()
{
    static const QStringList styles{QStringLiteral("bearer"), QStringLiteral("x-api-key"),
                                    QStringLiteral("query-key")};
    return styles;
}

const QStringList &knownValidationMethods()
{
    static const QStringList methods{QStringLiteral("models"), QStringLiteral("chat-minimal")};
    return methods;
}

/// 只保留字符串值，避免把非字符串塞进请求头
QJsonObject sanitizedHeaders(const QJsonValue &value)
{
    QJsonObject result;
    const QJsonObject source = value.toObject();
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        if (it.value().isString() && !it.key().isEmpty())
            result.insert(it.key(), it.value().toString());
    }
    return result;
}

/**
 * 解析单个模型的 thinkingSpec。
 * 返回 false 表示**档位声明与原生映射不一致**（漂移），该模型条目应被丢弃，
 * 以免向厂商下发错误的请求体；细节写入 warning。
 */
bool parseThinkingSpec(const QJsonValue &value, CatalogThinkingSpec *spec, QString *warning,
                       const QString &modelId)
{
    spec->levels.clear();
    spec->native = QJsonObject();

    const QJsonObject object = value.toObject();
    const QJsonArray levels = object.value(QStringLiteral("levels")).toArray();
    for (const QJsonValue &level : levels) {
        const QString name = level.toString().trimmed().toLower();
        if (name.isEmpty())
            continue;
        if (!knownLevels().contains(name)) {
            if (warning) {
                *warning = QStringLiteral("模型 %1 声明了未知档位「%2」，已忽略该档位")
                               .arg(modelId, name);
            }
            continue;
        }
        if (!spec->levels.contains(name))
            spec->levels.append(name);
    }

    const QJsonObject native = object.value(QStringLiteral("native")).toObject();

    // 空档位 => 原生映射也必须为空
    if (spec->levels.isEmpty()) {
        if (!native.isEmpty()) {
            if (warning) {
                *warning = QStringLiteral("模型 %1 未声明任何档位，却给出了原生映射（contract 漂移）")
                               .arg(modelId);
            }
            return false;
        }
        return true;
    }

    // 双向一一对应：缺档位或多出未知档位都视为漂移
    for (const QString &level : spec->levels) {
        if (!native.contains(level)) {
            if (warning) {
                *warning = QStringLiteral("模型 %1 的档位「%2」缺少原生映射（contract 漂移）")
                               .arg(modelId, level);
            }
            return false;
        }
    }
    for (auto it = native.constBegin(); it != native.constEnd(); ++it) {
        if (!spec->levels.contains(it.key())) {
            if (warning) {
                *warning = QStringLiteral("模型 %1 的原生映射含未声明档位「%2」（contract 漂移）")
                               .arg(modelId, it.key());
            }
            return false;
        }
    }

    spec->native = native;
    return true;
}

}  // namespace

QJsonObject CatalogThinkingSpec::nativeFor(const QString &level) const
{
    const QJsonValue value = native.value(level);
    if (value.isUndefined() || value.isNull())
        return QJsonObject();
    return value.toObject();
}

CatalogModel CatalogProvider::model(const QString &modelId) const
{
    for (const CatalogModel &item : models) {
        if (item.id == modelId)
            return item;
    }
    return CatalogModel();
}

QString CatalogProvider::defaultModelId() const
{
    for (const CatalogModel &item : models) {
        if (item.isDefault)
            return item.id;
    }
    return models.isEmpty() ? QString() : models.first().id;
}

ProviderCatalog::ProviderCatalog(QString resourcePath, QObject *parent)
    : QObject(parent)
    , m_resourcePath(std::move(resourcePath))
{
}

QString ProviderCatalog::defaultResourcePath()
{
    return QStringLiteral(":/providers/providers.json");
}

bool ProviderCatalog::available() const
{
    return m_available;
}

QString ProviderCatalog::lastError() const
{
    return m_lastError;
}

QStringList ProviderCatalog::warnings() const
{
    return m_warnings;
}

QList<CatalogProvider> ProviderCatalog::providers() const
{
    return m_providers;
}

QStringList ProviderCatalog::providerIds() const
{
    QStringList ids;
    ids.reserve(m_providers.size());
    for (const CatalogProvider &provider : m_providers)
        ids.append(provider.id);
    return ids;
}

CatalogProvider ProviderCatalog::provider(const QString &providerId) const
{
    const QString id = providerId.trimmed();
    for (const CatalogProvider &item : m_providers) {
        if (item.id == id)
            return item;
    }
    // 未知 id 退到「自定义」逃生口，保证上层永远拿到有效配置而非悬空引用
    for (const CatalogProvider &item : m_providers) {
        if (item.customizable)
            return item;
    }
    return CatalogProvider();
}

bool ProviderCatalog::load(QString *error)
{
    m_providers.clear();
    m_warnings.clear();
    m_available = false;
    setLastError(QString());
    if (error)
        error->clear();

    QFile file(m_resourcePath);
    if (!file.exists()) {
        setLastError(QStringLiteral("目录文件缺失：%1").arg(m_resourcePath));
        if (error)
            *error = m_lastError;
        emit changed();
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("目录文件无法读取：%1").arg(file.errorString()));
        if (error)
            *error = m_lastError;
        emit changed();
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("目录文件已损坏（%1）").arg(parseError.errorString()));
        if (error)
            *error = m_lastError;
        emit changed();
        return false;
    }

    const QJsonArray entries = document.object().value(QStringLiteral("providers")).toArray();
    for (const QJsonValue &entry : entries) {
        const QJsonObject object = entry.toObject();

        CatalogProvider provider;
        provider.id = object.value(QStringLiteral("id")).toString().trimmed();
        if (provider.id.isEmpty()) {
            m_warnings.append(QStringLiteral("跳过缺少 id 的厂商条目"));
            continue;
        }

        const QString protocol = object.value(QStringLiteral("protocol")).toString().trimmed();
        if (!knownProtocols().contains(protocol)) {
            m_warnings.append(QStringLiteral("厂商 %1 的 protocol「%2」不受支持，已跳过")
                                  .arg(provider.id, protocol));
            continue;
        }
        provider.protocol = protocol;
        provider.displayName =
            object.value(QStringLiteral("displayName")).toString(provider.id);
        provider.baseUrl = object.value(QStringLiteral("baseUrl")).toString().trimmed();
        provider.customizable = object.value(QStringLiteral("customizable")).toBool(false);

        // 除「自定义」外必须给出明确 baseUrl：不在代码里隐藏厂商默认域名
        if (provider.baseUrl.isEmpty() && !provider.customizable) {
            m_warnings.append(QStringLiteral("厂商 %1 缺少 baseUrl，已跳过").arg(provider.id));
            continue;
        }

        provider.authStyle = object.value(QStringLiteral("authStyle")).toString().trimmed();
        if (!knownAuthStyles().contains(provider.authStyle)) {
            m_warnings.append(QStringLiteral("厂商 %1 的 authStyle「%2」未知，按 bearer 处理")
                                  .arg(provider.id, provider.authStyle));
            provider.authStyle = QStringLiteral("bearer");
        }

        provider.extraHeaders =
            sanitizedHeaders(object.value(QStringLiteral("extraHeaders")));

        const QJsonObject validation = object.value(QStringLiteral("validation")).toObject();
        provider.validationMethod = validation.value(QStringLiteral("method")).toString().trimmed();
        if (!knownValidationMethods().contains(provider.validationMethod)) {
            m_warnings.append(QStringLiteral("厂商 %1 的 validation.method「%2」未知，按 chat-minimal 处理")
                                  .arg(provider.id, provider.validationMethod));
            provider.validationMethod = QStringLiteral("chat-minimal");
        }

        // 内置联网搜索（可选）：声明了 searchEndpoint 才视为可用；非法条目只记警告、不阻断加载
        const QJsonObject nativeSearch = object.value(QStringLiteral("nativeSearch")).toObject();
        if (!nativeSearch.isEmpty()) {
            CatalogNativeSearch spec;
            spec.protocol = nativeSearch.value(QStringLiteral("protocol")).toString().trimmed().toLower();
            spec.searchEndpoint = nativeSearch.value(QStringLiteral("searchEndpoint")).toString().trimmed();
            spec.fetchEndpoint = nativeSearch.value(QStringLiteral("fetchEndpoint")).toString().trimmed();
            spec.reuseProviderKey = nativeSearch.value(QStringLiteral("reuseProviderKey")).toBool(false);

            if (spec.searchEndpoint.isEmpty()) {
                m_warnings.append(QStringLiteral("厂商 %1 的 nativeSearch 缺少 searchEndpoint，已忽略该声明")
                                      .arg(provider.id));
            } else if (!spec.searchEndpoint.startsWith(QStringLiteral("http"))) {
                m_warnings.append(QStringLiteral("厂商 %1 的 nativeSearch.searchEndpoint 非 http(s) 地址，已忽略该声明")
                                      .arg(provider.id));
            } else {
                spec.available = true;
                provider.nativeSearch = spec;
            }
        }

        // 向量化能力（可选）：资料库用；条目非法只跳过该条并记警告
        const QJsonObject embedding = object.value(QStringLiteral("embedding")).toObject();
        provider.embedding.available = embedding.value(QStringLiteral("available")).toBool(false);
        for (const QJsonValue &embeddingEntry :
             embedding.value(QStringLiteral("models")).toArray()) {
            const QJsonObject embeddingObject = embeddingEntry.toObject();
            CatalogEmbeddingModel embeddingModel;
            embeddingModel.id = embeddingObject.value(QStringLiteral("id")).toString().trimmed();
            if (embeddingModel.id.isEmpty()) {
                m_warnings.append(QStringLiteral("厂商 %1 下跳过缺少 id 的 embedding 模型")
                                      .arg(provider.id));
                continue;
            }
            embeddingModel.displayName =
                embeddingObject.value(QStringLiteral("displayName")).toString(embeddingModel.id);
            embeddingModel.dimensions =
                embeddingObject.value(QStringLiteral("dimensions")).toInt(0);
            embeddingModel.isDefault =
                embeddingObject.value(QStringLiteral("isDefault")).toBool(false);
            provider.embedding.models.append(embeddingModel);
        }
        if (!provider.embedding.models.isEmpty())
            provider.embedding.available = true;

        // 图片 caption 的建议视觉模型（可选）：仅作 UI 预填，不强制
        provider.captionModel =
            object.value(QStringLiteral("vision"))
                .toObject()
                .value(QStringLiteral("captionModel"))
                .toString()
                .trimmed();

        const QJsonArray models = object.value(QStringLiteral("models")).toArray();
        for (const QJsonValue &modelEntry : models) {
            const QJsonObject modelObject = modelEntry.toObject();

            CatalogModel model;
            model.id = modelObject.value(QStringLiteral("id")).toString().trimmed();
            if (model.id.isEmpty()) {
                m_warnings.append(
                    QStringLiteral("厂商 %1 下跳过缺少 id 的模型条目").arg(provider.id));
                continue;
            }
            model.displayName =
                modelObject.value(QStringLiteral("displayName")).toString(model.id);
            model.supportsVision =
                modelObject.value(QStringLiteral("supportsVision")).toBool(false);
            model.isDefault = modelObject.value(QStringLiteral("isDefault")).toBool(false);

            QString warning;
            if (!parseThinkingSpec(modelObject.value(QStringLiteral("thinkingSpec")), &model.thinking,
                                   &warning, model.id)) {
                m_warnings.append(QStringLiteral("厂商 %1：%2，已跳过该模型")
                                      .arg(provider.id, warning));
                continue;
            }
            if (!warning.isEmpty())
                m_warnings.append(QStringLiteral("厂商 %1：%2").arg(provider.id, warning));

            provider.models.append(model);
        }

        if (provider.models.isEmpty() && !provider.customizable) {
            m_warnings.append(
                QStringLiteral("厂商 %1 没有任何可用模型，UI 将无法选择模型").arg(provider.id));
        }

        m_providers.append(provider);
    }

    if (m_providers.isEmpty()) {
        setLastError(QStringLiteral("目录中没有任何可用厂商条目"));
        if (error)
            *error = m_lastError;
        emit changed();
        return false;
    }

    m_available = true;
    emit changed();
    return true;
}

QString CatalogEmbedding::defaultModelId() const
{
    for (const CatalogEmbeddingModel &item : models) {
        if (item.isDefault)
            return item.id;
    }
    return models.isEmpty() ? QString() : models.first().id;
}

CatalogEmbeddingModel CatalogEmbedding::model(const QString &modelId) const
{
    for (const CatalogEmbeddingModel &item : models) {
        if (item.id == modelId)
            return item;
    }
    return CatalogEmbeddingModel();
}

QVariantList ProviderCatalog::providerOptions() const
{
    QVariantList options;
    options.reserve(m_providers.size());
    for (const CatalogProvider &provider : m_providers) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), provider.id);
        map.insert(QStringLiteral("displayName"), provider.displayName);
        map.insert(QStringLiteral("protocol"), provider.protocol);
        map.insert(QStringLiteral("customizable"), provider.customizable);
        map.insert(QStringLiteral("authStyle"), provider.authStyle);
        map.insert(QStringLiteral("validationMethod"), provider.validationMethod);
        map.insert(QStringLiteral("nativeSearch"), provider.nativeSearch.isValid());
        map.insert(QStringLiteral("modelCount"), provider.models.size());
        options.append(map);
    }
    return options;
}

QVariantList ProviderCatalog::modelOptions(const QString &providerId) const
{
    const CatalogProvider entry = provider(providerId);
    QVariantList options;
    options.reserve(entry.models.size());
    for (const CatalogModel &model : entry.models) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), model.id);
        map.insert(QStringLiteral("displayName"), model.displayName);
        map.insert(QStringLiteral("supportsVision"), model.supportsVision);
        map.insert(QStringLiteral("isDefault"), model.isDefault);
        map.insert(QStringLiteral("levels"), model.thinking.levels);
        map.insert(QStringLiteral("supportsThinking"), model.thinking.supportsThinking());
        options.append(map);
    }
    return options;
}

QVariantMap ProviderCatalog::modelInfo(const QString &providerId, const QString &modelId) const
{
    const CatalogProvider entry = provider(providerId);
    const CatalogModel model = entry.model(modelId);
    // 无效模型返回空表，UI 可直接以 isEmpty() 判定，不必逐字段试探
    if (!model.isValid())
        return QVariantMap();

    QVariantMap map;
    map.insert(QStringLiteral("id"), model.id);
    map.insert(QStringLiteral("displayName"), model.displayName);
    map.insert(QStringLiteral("supportsVision"), model.supportsVision);
    map.insert(QStringLiteral("supportsThinking"), model.thinking.supportsThinking());
    map.insert(QStringLiteral("levels"), model.thinking.levels);
    map.insert(QStringLiteral("native"), model.thinking.native.toVariantMap());
    return map;
}

void ProviderCatalog::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit changed();
}
