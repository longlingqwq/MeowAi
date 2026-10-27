#include "core/skill/SkillRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <utility>

QString skillSourceLabel(SkillSource source)
{
    return source == SkillSource::Bundled ? QStringLiteral("内置") : QStringLiteral("导入");
}

namespace {

/// 基于本地 manifest.json 的 Skill 实现（仅在本文件内可见）
class LocalFileSkill final : public Skill
{
public:
    LocalFileSkill(SkillManifest manifest, QString directory, SkillSource source)
        : m_manifest(std::move(manifest))
        , m_directory(std::move(directory))
        , m_source(source)
    {
    }

    SkillManifest manifest() const override { return m_manifest; }
    QString sourcePath() const override { return m_directory; }
    SkillSource source() const override { return m_source; }

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
    SkillSource m_source = SkillSource::Bundled;
};

/// 解析 triggers：缺省为空表；存在但类型不对必须报错，不做猜测
bool parseTriggers(const QJsonObject &object, QStringList *out, QString *reason)
{
    out->clear();
    if (!object.contains(QStringLiteral("triggers"))) {
        return true;
    }

    const QJsonValue value = object.value(QStringLiteral("triggers"));
    if (!value.isArray()) {
        *reason = QStringLiteral("triggers 必须是字符串数组");
        return false;
    }

    const QJsonArray array = value.toArray();
    QStringList seen;
    for (const QJsonValue &item : array) {
        if (!item.isString()) {
            *reason = QStringLiteral("triggers 必须是字符串数组");
            return false;
        }
        const QString trigger = item.toString().trimmed();
        if (trigger.isEmpty()) {
            continue;
        }
        // 去重但不改变书写：同一触发词只保留首次出现
        if (seen.contains(trigger, Qt::CaseInsensitive)) {
            continue;
        }
        seen << trigger;
        out->append(trigger);
    }
    return true;
}

} // namespace

namespace SkillManifestRules {

bool isValidId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9-]+$"));
    return pattern.match(id).hasMatch();
}

bool isSafeRelativeEntry(const QString &entry)
{
    if (entry.isEmpty()) {
        return false;
    }
    // 反斜杠统一按分隔符处理，避免 Windows 风格路径绕过检查
    const QString normalized = QString(entry).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalized.startsWith(QLatin1Char('/'))) {
        return false;
    }
    if (normalized.size() > 1 && normalized.at(1) == QLatin1Char(':')) {
        return false; // 盘符绝对路径
    }
    const QStringList segments = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        if (segment == QStringLiteral("..")) {
            return false;
        }
    }
    return !segments.isEmpty();
}

bool parseObject(const QJsonObject &object, SkillManifest *out, QString *reason)
{
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    const QString version = object.value(QStringLiteral("version")).toString().trimmed();
    const QString entry = object.value(QStringLiteral("entry")).toString().trimmed();

    if (id.isEmpty() || name.isEmpty() || version.isEmpty() || entry.isEmpty()) {
        *reason = QStringLiteral("manifest 缺少必填字段（id/name/version/entry）");
        return false;
    }
    if (!isValidId(id)) {
        *reason = QStringLiteral("manifest.id 只能包含字母、数字与连字符");
        return false;
    }
    if (!isSafeRelativeEntry(entry)) {
        *reason = QStringLiteral("manifest.entry 必须是技能目录内的相对路径");
        return false;
    }

    QStringList triggers;
    if (!parseTriggers(object, &triggers, reason)) {
        return false;
    }

    out->id = id;
    out->name = name;
    out->version = version;
    out->description = object.value(QStringLiteral("description")).toString().trimmed();
    out->entry = entry;
    out->triggers = triggers;
    return true;
}

bool parseFile(const QString &manifestPath, SkillManifest *out, QString *reason)
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

    return parseObject(doc.object(), out, reason);
}

QString exampleJson()
{
    return QStringLiteral(
        "{\n"
        "  \"id\": \"my-skill\",\n"
        "  \"name\": \"我的技能\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"description\": \"一句话说明这个技能做什么\",\n"
        "  \"entry\": \"skill.md\",\n"
        "  \"triggers\": [\"关键词一\", \"关键词二\"]\n"
        "}");
}

} // namespace SkillManifestRules

SkillRegistry::SkillRegistry(QString skillsRoot, SkillSource source)
    : m_root(std::move(skillsRoot))
    , m_source(source)
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
        if (!SkillManifestRules::parseFile(manifestPath, &manifest, &reason)) {
            if (warnings) {
                *warnings << QStringLiteral("跳过 %1：%2").arg(dirName, reason);
            }
            continue;
        }

        if (manifest.id != dirName && warnings) {
            *warnings << QStringLiteral("%1：manifest.id 与目录名不一致").arg(dirName);
        }

        m_skills.push_back(
            std::make_unique<LocalFileSkill>(std::move(manifest), directory, m_source));
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

SkillSource SkillRegistry::source() const
{
    return m_source;
}

QList<SkillManifest> SkillRegistry::manifests() const
{
    QList<SkillManifest> result;
    result.reserve(static_cast<int>(m_skills.size()));
    for (const auto &skill : m_skills) {
        result.append(skill->manifest());
    }
    return result;
}
