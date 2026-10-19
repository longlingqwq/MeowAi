#include "core/skill/SkillRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <utility>

namespace {

/// 基于本地 manifest.json 的 Skill 实现（仅在本文件内可见）
class LocalFileSkill final : public Skill
{
public:
    LocalFileSkill(SkillManifest manifest, QString directory)
        : m_manifest(std::move(manifest))
        , m_directory(std::move(directory))
    {
    }

    SkillManifest manifest() const override { return m_manifest; }
    QString sourcePath() const override { return m_directory; }

    QString readDocument(QString *error) const override
    {
        const QString path = QDir(m_directory).filePath(m_manifest.entry);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) {
                *error = QStringLiteral("无法读取技能文档：%1").arg(path);
            }
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

private:
    SkillManifest m_manifest;
    QString m_directory;
};

/// 解析 manifest；字段缺失或 JSON 损坏时返回 false 并写入原因
bool parseManifest(const QString &manifestPath, SkillManifest *out, QString *reason)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *reason = QStringLiteral("无法读取 manifest：%1").arg(manifestPath);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *reason = QStringLiteral("manifest JSON 非法：%1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString name = obj.value(QStringLiteral("name")).toString();
    const QString version = obj.value(QStringLiteral("version")).toString();
    const QString entry = obj.value(QStringLiteral("entry")).toString();

    if (id.isEmpty() || name.isEmpty() || version.isEmpty() || entry.isEmpty()) {
        *reason = QStringLiteral("manifest 缺少必填字段（id/name/version/entry）");
        return false;
    }

    out->id = id;
    out->name = name;
    out->version = version;
    out->description = obj.value(QStringLiteral("description")).toString();
    out->entry = entry;
    return true;
}

} // namespace

SkillRegistry::SkillRegistry(QString skillsRoot)
    : m_root(std::move(skillsRoot))
{
}

int SkillRegistry::scan(QStringList *warnings)
{
    m_skills.clear();

    const QDir root(m_root);
    if (!root.exists()) {
        if (warnings) {
            *warnings << QStringLiteral("Skill 目录不存在：%1").arg(m_root);
        }
        return 0;
    }

    const QStringList entries =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString &dirName : entries) {
        const QString directory = root.filePath(dirName);
        const QString manifestPath = QDir(directory).filePath(QStringLiteral("manifest.json"));

        if (!QFileInfo::exists(manifestPath)) {
            if (warnings) {
                *warnings << QStringLiteral("跳过 %1：缺少 manifest.json").arg(dirName);
            }
            continue;
        }

        SkillManifest manifest;
        QString reason;
        if (!parseManifest(manifestPath, &manifest, &reason)) {
            if (warnings) {
                *warnings << QStringLiteral("跳过 %1：%2").arg(dirName, reason);
            }
            continue;
        }

        if (manifest.id != dirName && warnings) {
            *warnings << QStringLiteral("%1：manifest.id 与目录名不一致").arg(dirName);
        }

        m_skills.push_back(std::make_unique<LocalFileSkill>(std::move(manifest), directory));
    }

    return static_cast<int>(m_skills.size());
}

int SkillRegistry::count() const
{
    return static_cast<int>(m_skills.size());
}

QStringList SkillRegistry::ids() const
{
    QStringList result;
    result.reserve(static_cast<int>(m_skills.size()));
    for (const auto &skill : m_skills) {
        result << skill->manifest().id;
    }
    return result;
}

const Skill *SkillRegistry::find(const QString &id) const
{
    for (const auto &skill : m_skills) {
        if (skill->manifest().id == id) {
            return skill.get();
        }
    }
    return nullptr;
}

QString SkillRegistry::root() const
{
    return m_root;
}
