#include "storage/WebConfigStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <utility>

namespace {

constexpr auto kSection = "web";
constexpr auto kKeyEnabled = "enabled";
constexpr auto kKeyEndpoint = "endpoint";

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
        if (error) {
            *error = QStringLiteral("配置文件已损坏（%1），联网检索配置已回退默认")
                         .arg(parseError.errorString());
        }
        return QJsonObject();
    }
    return document.object();
}

}  // namespace

WebConfigStore::WebConfigStore(QString configFile)
    : m_configFile(std::move(configFile))
{
}

WebConfigData WebConfigStore::load(QString *error) const
{
    if (error)
        error->clear();

    WebConfigData data;
    const QJsonObject root = readRoot(m_configFile, error);
    const QJsonObject section = root.value(QLatin1String(kSection)).toObject();
    if (section.isEmpty())
        return data;

    data.enabled = section.value(QLatin1String(kKeyEnabled)).toBool(false);
    data.endpoint = readString(section, QLatin1String(kKeyEndpoint));
    return data;
}

bool WebConfigStore::save(const WebConfigData &data, QString *error) const
{
    if (error)
        error->clear();
    if (m_configFile.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("配置路径为空，联网检索配置无法保存");
        return false;
    }

    // 以现有内容为基底，确保其他模块（ai / personalization / theme）的段不被覆盖
    QJsonObject root = readRoot(m_configFile, error);

    QJsonObject section;
    section.insert(QLatin1String(kKeyEnabled), data.enabled);
    section.insert(QLatin1String(kKeyEndpoint), data.endpoint);
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
