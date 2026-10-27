#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * Skill 规范（功能 #9）。
 *
 * 目录约定：<root>/<id>/ 下放置 manifest.json 与技能文档；
 * 放入即被自动扫描加载，无需改代码。
 *  - 内置：resources/skills/<id>/，运行期经 ":/skills" 读取；
 *  - 用户：%APPDATA%/MeowAi/skills/<id>/，由技能仓库导入。
 *
 * manifest.json 字段：
 * {
 *   "id":          "example",     // 必填，唯一标识，须与目录名一致；仅字母、数字、连字符
 *   "name":        "示例技能",     // 必填，展示名
 *   "version":     "0.1.0",       // 必填
 *   "description": "一句话说明",    // 选填
 *   "entry":       "skill.md",    // 必填，技能文档（相对 manifest 所在目录，禁止越级）
 *   "triggers":    ["示例", "example"]  // 自动模式必填：非空字符串数组
 * }
 */
struct SkillManifest
{
    QString id;
    QString name;
    QString version;
    QString description;
    QString entry;
    /// 自动模式触发词（不区分大小写，中文按子串匹配）；空表示不参与自动匹配
    QStringList triggers;
};

/// 技能来源：内置（qrc，只读） / 用户导入（AppData，可删）
enum class SkillSource
{
    Bundled,
    Imported
};

/// 来源的中文展示名
QString skillSourceLabel(SkillSource source);

/// Skill 抽象接口
class Skill
{
public:
    virtual ~Skill() = default;

    virtual SkillManifest manifest() const = 0;

    /// manifest.json 所在目录
    virtual QString sourcePath() const = 0;

    virtual SkillSource source() const = 0;

    /// 读取技能文档内容（供后续注入给 AI）；失败返回空串并写入 error
    virtual QString readDocument(QString *error) const = 0;
};

/**
 * manifest 字段口径的唯一实现：扫描与导入共用，避免两处规则漂移。
 * 所有函数都不抛异常，失败时把中文原因写入 reason。
 */
namespace SkillManifestRules {

/// id 只允许字母、数字与连字符（且不得为空）
bool isValidId(const QString &id);

/// entry 必须是相对路径，且不得越级（..）或为绝对路径
bool isSafeRelativeEntry(const QString &entry);

/// 解析 JSON 对象；必填字段缺失、triggers 类型错误都返回 false
bool parseObject(const QJsonObject &object, SkillManifest *out, QString *reason);

/// 解析 manifest.json 文件；读不到 / JSON 非法 / 字段不合法都返回 false
bool parseFile(const QString &manifestPath, SkillManifest *out, QString *reason);

/// 导入失败时展示的正确示例（字段口径以本文件头注释为准，不得猜测）
QString exampleJson();

} // namespace SkillManifestRules
