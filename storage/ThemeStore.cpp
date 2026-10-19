#include "storage/ThemeStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

namespace {

constexpr auto kSection = "theme";
constexpr auto kKeyPreset = "preset";

/// 读取 config.json 根对象；缺失 / 损坏一律以空对象继续（不覆盖、不报致命错）
QJsonObject readRoot(const QString &path, QString *error)
{
    if (path.trimmed().isEmpty() || !QFile::exists(path)) {
        return QJsonObject();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("配置文件读取失败：%1").arg(file.errorString());
        }
        return QJsonObject();
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.trimmed().isEmpty()) {
        return QJsonObject();
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("配置文件已损坏（%1），主题选择已回退默认")
                         .arg(parseError.errorString());
        }
        return QJsonObject();
    }
    return document.object();
}

} // namespace

ThemeStore::ThemeStore(QString configFile)
    : m_configFile(std::move(configFile))
{
}

QString ThemeStore::loadPresetId(QString *error) const
{
    if (error) {
        error->clear();
    }
    const QJsonObject root = readRoot(m_configFile, error);
    const QJsonObject section = root.value(QLatin1String(kSection)).toObject();
    const QJsonValue value = section.value(QLatin1String(kKeyPreset));
    return value.isString() ? value.toString() : QString();
}

bool ThemeStore::savePresetId(const QString &presetId, QString *error) const
{
    if (error) {
        error->clear();
    }
    if (m_configFile.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("配置路径为空，主题选择无法保存");
        }
        return false;
    }

    // 读改写回：解析失败时以空对象重建，避免在损坏文件上继续追加内容
    QJsonObject root = readRoot(m_configFile, error);

    QJsonObject section = root.value(QLatin1String(kSection)).toObject();
    section.insert(QLatin1String(kKeyPreset), presetId);
    root.insert(QLatin1String(kSection), section);

    QSaveFile output(m_configFile);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("配置文件写入失败：%1").arg(output.errorString());
        }
        return false;
    }
    output.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        if (error) {
            *error = QStringLiteral("配置文件保存失败：%1").arg(output.errorString());
        }
        return false;
    }
    return true;
}
