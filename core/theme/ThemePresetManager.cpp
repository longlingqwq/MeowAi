#include "core/theme/ThemePresetManager.h"

#include "storage/ThemeStore.h"

#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaEnum>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariant>

#include <utility>

namespace {

constexpr auto kTokensKey = "tokens";
constexpr auto kDefaultPresetId = "theme1";

/// 令牌名 -> QML 单例 Theme 上的属性名（当前二者同名，保持一对一映射）
QString propertyNameForToken(const QString &token)
{
    return token;
}

} // namespace

ThemePresetManager::ThemePresetManager(ThemeStore *store, QString presetRoot, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_presetRoot(std::move(presetRoot))
{
}

ThemePresetManager::~ThemePresetManager() = default;

QString ThemePresetManager::currentPresetId() const
{
    return m_currentId;
}

QVariantList ThemePresetManager::presets() const
{
    QVariantList list;
    list.reserve(m_presets.size());
    for (const ThemePreset &preset : m_presets) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), preset.id);
        item.insert(QStringLiteral("name"), preset.name.isEmpty() ? preset.id : preset.name);
        list.append(item);
    }
    return list;
}

QString ThemePresetManager::lastError() const
{
    return m_lastError;
}

void ThemePresetManager::load()
{
    m_presets.clear();

    const QDir dir(m_presetRoot);
    const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        QFile handle(dir.filePath(file));
        if (!handle.open(QIODevice::ReadOnly)) {
            setLastError(QStringLiteral("主题预设不可读：%1").arg(file));
            continue;
        }
        const QByteArray data = handle.readAll();
        handle.close();

        ThemePreset preset;
        QString error;
        if (!parsePreset(data, &preset, &error)) {
            setLastError(QStringLiteral("主题预设解析失败（%1）：%2").arg(file, error));
            continue;
        }
        if (preset.id.isEmpty()) {
            preset.id = QFileInfo(file).completeBaseName();
        }
        m_presets.append(preset);
    }

    if (m_presets.isEmpty()) {
        setLastError(QStringLiteral("未找到任何主题预设，已使用 Theme.qml 内置默认配色"));
        m_currentId.clear();
        emit presetsChanged();
        emit currentPresetChanged();
        return;
    }

    QString error;
    QString selected = m_store ? m_store->loadPresetId(&error) : QString();
    if (selected.isEmpty() || !findPreset(selected)) {
        selected = m_presets.first().id;
        if (selected.isEmpty()) {
            selected = QLatin1String(kDefaultPresetId);
        }
    }
    if (!error.isEmpty()) {
        setLastError(error);
    }

    m_currentId = selected;
    emit presetsChanged();
    emit currentPresetChanged();
    applyCurrentPreset();
}

void ThemePresetManager::attachThemeObject(QObject *themeObject)
{
    m_themeObject = themeObject;
    if (!m_themeObject) {
        setLastError(QStringLiteral("未能定位设计令牌单例，主题预设未应用"));
        return;
    }
    applyCurrentPreset();
}

bool ThemePresetManager::selectPreset(const QString &id)
{
    const ThemePreset *preset = findPreset(id);
    if (!preset) {
        setLastError(QStringLiteral("主题不存在：%1").arg(id));
        return false;
    }

    m_currentId = preset->id;
    applyCurrentPreset();
    emit currentPresetChanged();

    if (m_store) {
        QString error;
        if (!m_store->savePresetId(m_currentId, &error)) {
            setLastError(error);
            return false;
        }
    }
    setLastError(QString());
    return true;
}

bool ThemePresetManager::parsePreset(const QByteArray &json, ThemePreset *preset, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!preset) {
        if (error) {
            *error = QStringLiteral("输出参数为空");
        }
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject tokens = root.value(QLatin1String(kTokensKey)).toObject();
    if (tokens.isEmpty()) {
        if (error) {
            *error = QStringLiteral("预设缺少 tokens 段");
        }
        return false;
    }

    preset->id = root.value(QStringLiteral("id")).toString();
    preset->name = root.value(QStringLiteral("name")).toString();
    preset->tokens.clear();

    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        bool ok = false;
        const QVariant value = tokenValue(it.value(), &ok);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("令牌 %1 的值无法解析").arg(it.key());
            }
            return false;
        }
        preset->tokens.insert(it.key(), value);
    }
    return true;
}

QVariant ThemePresetManager::tokenValue(const QJsonValue &value, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (text.startsWith(QLatin1Char('#'))) {
            const QColor color(text);
            if (!color.isValid()) {
                return QVariant();
            }
            if (ok) {
                *ok = true;
            }
            return QVariant::fromValue(color);
        }
        // 缓动：字符串名映射到 QEasingCurve::Type 的整数值（Theme.qml 中以 int 承载）
        const QMetaEnum meta = QMetaEnum::fromType<QEasingCurve::Type>();
        bool found = false;
        const int enumValue = meta.keyToValue(text.toUtf8().constData(), &found);
        if (!found) {
            return QVariant();
        }
        if (ok) {
            *ok = true;
        }
        return QVariant::fromValue(enumValue);
    }

    if (value.isDouble()) {
        const double number = value.toDouble();
        if (ok) {
            *ok = true;
        }
        // 令牌中的数值统一按整数承载（间距 / 圆角 / 字号 / 时长）
        return QVariant::fromValue(static_cast<int>(number));
    }

    return QVariant();
}

QStringList ThemePresetManager::applyToObject(QObject *target, const ThemePreset &preset) const
{
    QStringList missing;
    if (!target) {
        return missing;
    }
    const QMetaObject *meta = target->metaObject();
    for (auto it = preset.tokens.constBegin(); it != preset.tokens.constEnd(); ++it) {
        const QString property = propertyNameForToken(it.key());
        const int index = meta->indexOfProperty(property.toUtf8().constData());
        if (index < 0) {
            missing.append(it.key());
            continue;
        }
        target->setProperty(property.toUtf8().constData(), it.value());
    }
    return missing;
}

void ThemePresetManager::setLastError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

void ThemePresetManager::applyCurrentPreset()
{
    const ThemePreset *preset = findPreset(m_currentId);
    if (!preset) {
        return;
    }
    if (!m_themeObject) {
        return; // 尚未注入 QML 单例：预设将在 attachThemeObject 时应用
    }

    const QStringList missing = applyToObject(m_themeObject, *preset);
    if (!missing.isEmpty()) {
        // 令牌名是全局契约：预设与 Theme.qml 出现漂移必须显式暴露，而不是静默漏色
        setLastError(QStringLiteral("主题「%1」含 Theme.qml 未定义的令牌：%2")
                         .arg(preset->name.isEmpty() ? preset->id : preset->name,
                              missing.join(QStringLiteral(", "))));
    } else {
        setLastError(QString());
    }
}

const ThemePreset *ThemePresetManager::findPreset(const QString &id) const
{
    for (const ThemePreset &preset : m_presets) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}
