#include "storage/LibraryConfigStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <utility>

namespace {

constexpr auto kSection = "library";

constexpr auto kKeyEnabled = "enabled";
constexpr auto kKeyConsentGiven = "consentGiven";
constexpr auto kKeyConsentAt = "consentAt";
constexpr auto kKeyTopK = "topK";
constexpr auto kKeyPerFileLimit = "perFileLimit";
constexpr auto kKeyTokenBudget = "tokenBudget";
constexpr auto kKeySimilarity = "similarityThreshold";
constexpr auto kKeyNeighborRadius = "neighborRadius";
constexpr auto kKeyFtsFallback = "ftsFallback";
constexpr auto kKeyChunkSize = "chunkSize";
constexpr auto kKeyChunkOverlap = "chunkOverlap";
constexpr auto kKeyEmbedding = "embedding";
constexpr auto kKeyCaption = "caption";
constexpr auto kKeyMounted = "mountedCollections";

QString readString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

/// 读取配置根对象；缺失 / 损坏一律以空对象继续（不覆盖、不报致命错）
QJsonObject readRoot(const QString &path, QString *error)
{
    if (path.trimmed().isEmpty() || !QFile::exists(path))
        return QJsonObject();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("配置文件读取失败：%1").arg(file.errorString());
        return QJsonObject();
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.trimmed().isEmpty())
        return QJsonObject();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("配置文件已损坏（%1），资料库配置已回退默认")
                         .arg(parseError.errorString());
        return QJsonObject();
    }
    return document.object();
}

/// 数值读取：类型不符或缺失时保留默认值（避免被手工编辑的脏 JSON 打乱行为）
int readInt(const QJsonObject &object, const QString &key, int fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt() : fallback;
}

qint64 readInt64(const QJsonObject &object, const QString &key, qint64 fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? static_cast<qint64>(value.toDouble()) : fallback;
}

bool readBool(const QJsonObject &object, const QString &key, bool fallback)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

}  // namespace

LibraryConfigStore::LibraryConfigStore(QString configFile)
    : m_configFile(std::move(configFile))
{
}

LibraryConfigData LibraryConfigStore::load(QString *error) const
{
    if (error)
        error->clear();

    LibraryConfigData data;
    const QJsonObject root = readRoot(m_configFile, error);
    if (!root.contains(QLatin1String(kSection)))
        return data;

    const QJsonObject section = root.value(QLatin1String(kSection)).toObject();

    data.enabled = readBool(section, QLatin1String(kKeyEnabled), data.enabled);
    data.consentGiven = readBool(section, QLatin1String(kKeyConsentGiven), data.consentGiven);
    data.consentAt = readInt64(section, QLatin1String(kKeyConsentAt), data.consentAt);

    data.topK = readInt(section, QLatin1String(kKeyTopK), data.topK);
    data.perFileLimit = readInt(section, QLatin1String(kKeyPerFileLimit), data.perFileLimit);
    data.tokenBudget = readInt(section, QLatin1String(kKeyTokenBudget), data.tokenBudget);
    data.neighborRadius = readInt(section, QLatin1String(kKeyNeighborRadius), data.neighborRadius);
    data.ftsFallback = readBool(section, QLatin1String(kKeyFtsFallback), data.ftsFallback);
    data.chunkSize = readInt(section, QLatin1String(kKeyChunkSize), data.chunkSize);
    data.chunkOverlap = readInt(section, QLatin1String(kKeyChunkOverlap), data.chunkOverlap);

    const QJsonValue threshold = section.value(QLatin1String(kKeySimilarity));
    if (threshold.isDouble())
        data.similarityThreshold = threshold.toDouble();

    const QJsonObject embedding = section.value(QLatin1String(kKeyEmbedding)).toObject();
    data.embedding.providerId = readString(embedding, QStringLiteral("providerId"));
    data.embedding.modelId = readString(embedding, QStringLiteral("modelId"));
    data.embedding.dimensions = readInt(embedding, QStringLiteral("dimensions"), data.embedding.dimensions);
    data.embedding.batchSize = readInt(embedding, QStringLiteral("batchSize"), data.embedding.batchSize);

    const QJsonObject caption = section.value(QLatin1String(kKeyCaption)).toObject();
    data.caption.providerId = readString(caption, QStringLiteral("providerId"));
    data.caption.modelId = readString(caption, QStringLiteral("modelId"));
    data.caption.maxEdge = readInt(caption, QStringLiteral("maxEdge"), data.caption.maxEdge);
    data.caption.maxTokens = readInt(caption, QStringLiteral("maxTokens"), data.caption.maxTokens);
    const QString profile = readString(caption, QStringLiteral("promptProfile"));
    if (!profile.isEmpty())
        data.caption.promptProfile = profile;

    const QJsonArray mounted = section.value(QLatin1String(kKeyMounted)).toArray();
    for (const QJsonValue &value : mounted) {
        const qint64 id = static_cast<qint64>(value.toDouble());
        if (id > 0 && !data.mountedCollections.contains(id))
            data.mountedCollections.append(id);
    }

    return data;
}

bool LibraryConfigStore::save(const LibraryConfigData &data, QString *error) const
{
    if (error)
        error->clear();
    if (m_configFile.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("配置路径为空，资料库配置无法保存");
        return false;
    }

    // 以现有内容为基底，确保其他模块（ai / theme / personalization / web）的段不被覆盖
    QJsonObject root = readRoot(m_configFile, error);

    QJsonObject embedding;
    embedding.insert(QStringLiteral("providerId"), data.embedding.providerId);
    embedding.insert(QStringLiteral("modelId"), data.embedding.modelId);
    embedding.insert(QStringLiteral("dimensions"), data.embedding.dimensions);
    embedding.insert(QStringLiteral("batchSize"), data.embedding.batchSize);

    QJsonObject caption;
    caption.insert(QStringLiteral("providerId"), data.caption.providerId);
    caption.insert(QStringLiteral("modelId"), data.caption.modelId);
    caption.insert(QStringLiteral("maxEdge"), data.caption.maxEdge);
    caption.insert(QStringLiteral("maxTokens"), data.caption.maxTokens);
    caption.insert(QStringLiteral("promptProfile"), data.caption.promptProfile);

    QJsonArray mounted;
    for (qint64 id : data.mountedCollections)
        mounted.append(static_cast<double>(id));

    QJsonObject section;
    section.insert(QLatin1String(kKeyEnabled), data.enabled);
    section.insert(QLatin1String(kKeyConsentGiven), data.consentGiven);
    section.insert(QLatin1String(kKeyConsentAt), static_cast<double>(data.consentAt));
    section.insert(QLatin1String(kKeyTopK), data.topK);
    section.insert(QLatin1String(kKeyPerFileLimit), data.perFileLimit);
    section.insert(QLatin1String(kKeyTokenBudget), data.tokenBudget);
    section.insert(QLatin1String(kKeySimilarity), data.similarityThreshold);
    section.insert(QLatin1String(kKeyNeighborRadius), data.neighborRadius);
    section.insert(QLatin1String(kKeyFtsFallback), data.ftsFallback);
    section.insert(QLatin1String(kKeyChunkSize), data.chunkSize);
    section.insert(QLatin1String(kKeyChunkOverlap), data.chunkOverlap);
    section.insert(QLatin1String(kKeyEmbedding), embedding);
    section.insert(QLatin1String(kKeyCaption), caption);
    section.insert(QLatin1String(kKeyMounted), mounted);
    root.insert(QLatin1String(kSection), section);

    QSaveFile output(m_configFile);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("配置文件写入失败：%1").arg(output.errorString());
        return false;
    }
    output.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        if (error)
            *error = QStringLiteral("配置文件保存失败：%1").arg(output.errorString());
        return false;
    }
    return true;
}
