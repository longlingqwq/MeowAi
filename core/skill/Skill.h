#pragma once

#include <QString>

/**
 * 内置 Skill 规范（功能 #9）。
 *
 * 目录约定：resources/skills/<id>/ 下放置 manifest.json 与技能文档；
 * 放入即被自动扫描加载，无需改代码。
 *
 * manifest.json 字段：
 * {
 *   "id":          "example",     // 必填，唯一标识，须与目录名一致
 *   "name":        "示例技能",     // 必填，展示名
 *   "version":     "0.1.0",       // 必填
 *   "description": "一句话说明",    // 选填
 *   "entry":       "skill.md"     // 必填，技能文档（相对 manifest 所在目录）
 * }
 */
struct SkillManifest
{
    QString id;
    QString name;
    QString version;
    QString description;
    QString entry;
};

/// Skill 抽象接口；阶段一仅定义契约，不实现调用逻辑
class Skill
{
public:
    virtual ~Skill() = default;

    virtual SkillManifest manifest() const = 0;

    /// manifest.json 所在目录
    virtual QString sourcePath() const = 0;

    /// 读取技能文档内容（供后续注入给 AI）；失败返回空串并写入 error
    virtual QString readDocument(QString *error) const = 0;
};
