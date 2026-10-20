#include "storage/AiConfigStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <utility>

namespace {

constexpr auto kSection = "ai";
constexpr auto kKeyProviderId = "providerId";
constexpr auto kKeyModelId = "modelId";
constexpr auto kKeyThinking = "thinkingLevels";
constexpr auto kKeyNative = "nativeOverrides";
constexpr auto kKeyCustomBaseUrl = "customBaseUrl";

/// 旧版（阶段二）配置遗留的扁平键，读取时做一次性迁移
constexpr auto kLegacySection = "model";
constexpr auto kLegacyProvider = "provider";
constexpr auto kLegacyName = "name";

QString readString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

/// 读取 config.json 根对象；缺失 / 损坏一律以空对象继续（不覆盖、不报致命错）
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
            *error = QStringLiteral("配置文件已损坏（%1），AI 接入配置已回退默认")
                         .arg(parseError.errorString());
        return QJsonObject();
    }
    return document.object();
}

}  // namespace

AiConfigStore::AiConfigStore(QString configFile)
    : m_configFile(std::move(configFile))
{
}

QString AiConfigStore::selectionKey(const QString &providerId, const QString &modelId)
{
    return providerId.trimmed() + QLatin1Char('.') + modelId.trimmed();
}

AiConfigData AiConfigStore::load(QString *error) const
{
    if (error)
        error->clear();

    AiConfigData data;
    const QJsonObject root = readRoot(m_configFile, error);
    QJsonObject section = root.value(QLatin1String(kSection)).toObject();

    // 迁移：`ai` 段不存在时，尝试读取阶段二遗留的 `model` 段（只迁移不删除旧键）
    if (section.isEmpty()) {
        const QJsonObject legacy = root.value(QLatin1String(kLegacySection)).toObject();
        if (!legacy.isEmpty()) {
            data.providerId = readString(legacy, QLatin1String(kLegacyProvider));
            data.modelId = readString(legacy, QLatin1String(kLegacyName));
        }
    }

    if (section.isEmpty())
        return data;

    data.providerId = readString(section, QLatin1String(kKeyProviderId));
    data.modelId = readString(section, QLatin1String(kKeyModelId));
    data.customBaseUrl = readString(section, QLatin1String(kKeyCustomBaseUrl));

    const QJsonObject thinking = section.value(QLatin1String(kKeyThinking)).toObject();
    for (auto it = thinking.constBegin(); it != thinking.constEnd(); ++it) {
        if (it.value().isString() && !it.key().isEmpty())
            data.thinkingLevels.insert(it.key(), it.value().toString());
    }

    const QJsonObject native = section.value(QLatin1String(kKeyNative)).toObject();
    for (auto it = native.constBegin(); it != native.constEnd(); ++it) {
        if (it.value().isObject() && !it.key().isEmpty())
            data.nativeOverrides.insert(it.key(), it.value().toObject());
    }

    return data;
}

bool AiConfigStore::save(const AiConfigData &data, QString *error) const
{
    if (error)
        error->clear();
    if (m_configFile.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("配置路径为空，AI 接入配置无法保存");
        return false;
    }

    // 以现有内容为基底，确保其他模块（personalization / theme）的段不被覆盖
    QJsonObject root = readRoot(m_configFile, error);

    QJsonObject thinking;
    for (auto it = data.thinkingLevels.constBegin(); it != data.thinkingLevels.constEnd(); ++it)
        thinking.insert(it.key(), it.value());

    QJsonObject native;
    for (auto it = data.nativeOverrides.constBegin(); it != data.nativeOverrides.constEnd(); ++it)
        native.insert(it.key(), it.value());

    QJsonObject section;
    section.insert(QLatin1String(kKeyProviderId), data.providerId);
    section.insert(QLatin1String(kKeyModelId), data.modelId);
    section.insert(QLatin1String(kKeyCustomBaseUrl), data.customBaseUrl);
    section.insert(QLatin1String(kKeyThinking), thinking);
    section.insert(QLatin1String(kKeyNative), native);
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
