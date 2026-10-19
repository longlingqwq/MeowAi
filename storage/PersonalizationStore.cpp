#include "storage/PersonalizationStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStringList>

#include <utility>

namespace {

constexpr auto kSection = "personalization";
constexpr auto kKeyFontFamily = "font_family";
constexpr auto kKeyFontSource = "font_source";
constexpr auto kKeyBackground = "background_source";
constexpr auto kKeyAvatar = "avatar_source";
constexpr auto kKeySplash = "splash_source";
constexpr auto kKeySplashEnabled = "splash_enabled";
constexpr auto kKeySplashSound = "splash_sound_enabled";

QString readString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

/// 读取路径字段：不合法（绝对路径 / 路径穿越）时忽略并记录警告，避免越权读写
QString readPath(const QJsonObject &object, const QString &key, QString *warning)
{
    const QString path = readString(object, key);
    if (path.isEmpty()) {
        return QString();
    }
    if (!PersonalizationStore::isSafeRelativePath(path)) {
        if (warning) {
            *warning = QStringLiteral("个性化配置中的资源路径不合法，已忽略：%1").arg(path);
        }
        return QString();
    }
    return path;
}

PersonalizationConfig fromJson(const QJsonObject &section, QString *warning)
{
    PersonalizationConfig config;
    config.fontSource = readPath(section, kKeyFontSource, warning);
    config.backgroundSource = readPath(section, kKeyBackground, warning);
    config.avatarSource = readPath(section, kKeyAvatar, warning);
    config.splashSource = readPath(section, kKeySplash, warning);
    config.fontFamily = readString(section, kKeyFontFamily);
    config.splashEnabled = section.value(kKeySplashEnabled).toBool(false);
    config.splashSoundEnabled = section.value(kKeySplashSound).toBool(true);
    return config;
}

QJsonObject toJson(const PersonalizationConfig &config)
{
    QJsonObject section;
    section.insert(kKeyFontFamily, config.fontFamily);
    section.insert(kKeyFontSource, config.fontSource);
    section.insert(kKeyBackground, config.backgroundSource);
    section.insert(kKeyAvatar, config.avatarSource);
    section.insert(kKeySplash, config.splashSource);
    section.insert(kKeySplashEnabled, config.splashEnabled);
    section.insert(kKeySplashSound, config.splashSoundEnabled);
    return section;
}

} // namespace

PersonalizationStore::PersonalizationStore(QString configFile)
    : m_configFile(std::move(configFile))
{
}

PersonalizationConfig PersonalizationStore::load(QString *error) const
{
    if (error) {
        error->clear();
    }

    // 首次运行没有 config.json 属正常情况，直接使用默认值
    if (m_configFile.trimmed().isEmpty() || !QFile::exists(m_configFile)) {
        return PersonalizationConfig{};
    }

    QFile file(m_configFile);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("个性化配置读取失败：%1").arg(file.errorString());
        }
        return PersonalizationConfig{};
    }
    const QByteArray data = file.readAll();
    file.close();

    if (data.trimmed().isEmpty()) {
        return PersonalizationConfig{};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("个性化配置已损坏（%1），已回退默认设置")
                         .arg(parseError.errorString());
        }
        return PersonalizationConfig{};
    }

    return fromJson(document.object().value(QLatin1String(kSection)).toObject(), error);
}

bool PersonalizationStore::save(const PersonalizationConfig &config, QString *error) const
{
    if (error) {
        error->clear();
    }
    if (m_configFile.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("配置路径为空，个性化设置无法保存");
        }
        return false;
    }

    // 边界校验：绝不把绝对路径或越权路径（..）写进配置文件
    auto rejectUnsafe = [error](const QString &key, const QString &value) {
        if (!value.isEmpty() && !PersonalizationStore::isSafeRelativePath(value)) {
            if (error) {
                *error = QStringLiteral("个性化配置包含非法资源路径（%1），已拒绝保存").arg(key);
            }
            return true;
        }
        return false;
    };
    if (rejectUnsafe(QLatin1String(kKeyFontSource), config.fontSource)
        || rejectUnsafe(QLatin1String(kKeyBackground), config.backgroundSource)
        || rejectUnsafe(QLatin1String(kKeyAvatar), config.avatarSource)
        || rejectUnsafe(QLatin1String(kKeySplash), config.splashSource)) {
        return false;
    }

    // 以现有内容为基底，确保其他模块的键不被覆盖；解析失败时以空对象重建，
    // 避免在损坏文件上继续追加内容。
    QJsonObject root;
    QFile file(m_configFile);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral("配置文件读取失败：%1").arg(file.errorString());
            }
            return false;
        }
        const QByteArray data = file.readAll();
        file.close();
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            root = document.object();
        }
    }

    root.insert(QLatin1String(kSection), toJson(config));

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

bool PersonalizationStore::isSafeRelativePath(const QString &relativePath)
{
    if (relativePath.trimmed().isEmpty()) {
        return false;
    }

    const QString normalized = QDir::fromNativeSeparators(relativePath);
    // 绝对路径（含 Windows 盘符、UNC、以 / 开头）与用户目录展开一律拒绝
    if (QDir::isAbsolutePath(normalized) || QFileInfo(normalized).isAbsolute()) {
        return false;
    }
    if (normalized.startsWith(QLatin1Char('~')) || normalized.contains(QLatin1Char(':'))) {
        return false;
    }

    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String("..")) {
            return false;
        }
    }
    return !parts.isEmpty();
}
