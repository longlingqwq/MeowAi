#include "core/skill/SkillStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

namespace {

constexpr int kStateVersion = 1;

} // namespace

SkillStateStore::SkillStateStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

QString SkillStateStore::filePath() const
{
    return m_filePath;
}

QString SkillStateStore::autoMode()
{
    return QStringLiteral("auto");
}

QString SkillStateStore::manualMode()
{
    return QStringLiteral("manual");
}

bool SkillStateStore::isValidMode(const QString &mode)
{
    return mode == autoMode() || mode == manualMode();
}

SkillStateData SkillStateStore::load(QString *error) const
{
    SkillStateData data;

    QFile file(m_filePath);
    if (!file.exists()) {
        // 首次运行：默认全部启用 + 自动模式，不算错误
        return data;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法读取技能状态文件：%1").arg(m_filePath);
        }
        return data;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("技能状态文件已损坏，已回退默认（全部启用 · 自动模式）");
        }
        return data;
    }

    const QJsonObject root = doc.object();

    const QString mode = root.value(QStringLiteral("mode")).toString();
    if (isValidMode(mode)) {
        data.mode = mode;
    } else if (!mode.isEmpty() && error) {
        *error = QStringLiteral("技能状态文件的模式取值非法，已回退自动模式");
    }

    const QJsonValue enabledValue = root.value(QStringLiteral("enabled"));
    if (enabledValue.isObject()) {
        const QJsonObject enabled = enabledValue.toObject();
        for (auto it = enabled.constBegin(); it != enabled.constEnd(); ++it) {
            if (it.value().isBool()) {
                data.enabled.insert(it.key(), it.value().toBool());
            }
        }
    } else if (!enabledValue.isUndefined() && error) {
        *error = QStringLiteral("技能状态文件的 enabled 字段格式非法，已按全部启用处理");
    }

    return data;
}

bool SkillStateStore::save(const SkillStateData &data, QString *error) const
{
    if (m_filePath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("技能状态文件路径为空，无法保存");
        }
        return false;
    }

    QDir parent = QFileInfo(m_filePath).absoluteDir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("无法创建技能状态目录：%1").arg(parent.absolutePath());
        }
        return false;
    }

    QJsonObject enabled;
    for (auto it = data.enabled.constBegin(); it != data.enabled.constEnd(); ++it) {
        enabled.insert(it.key(), it.value());
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kStateVersion);
    root.insert(QStringLiteral("mode"), isValidMode(data.mode) ? data.mode : autoMode());
    root.insert(QStringLiteral("enabled"), enabled);

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("无法写入技能状态文件：%1").arg(m_filePath);
        }
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error) {
            *error = QStringLiteral("技能状态写入不完整：%1").arg(m_filePath);
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("技能状态保存失败：%1").arg(m_filePath);
        }
        return false;
    }
    return true;
}
