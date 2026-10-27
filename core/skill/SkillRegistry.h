#pragma once

#include "core/skill/Skill.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

/**
 * Skill 注册器（功能 #9）：扫描指定根目录，按 manifest 装载 Skill。
 *
 * 目录不合法、manifest 缺字段或 JSON 损坏都只跳过并记录警告，绝不崩溃。
 * 同一实例只对应一个根目录；内置与用户目录由 SkillCatalog 各持一个实例合并。
 */
class SkillRegistry
{
public:
    explicit SkillRegistry(QString skillsRoot,
                           SkillSource source = SkillSource::Bundled);

    /// 扫描并装载；返回成功装载数量，问题统一汇总到 warnings
    int scan(QStringList *warnings = nullptr);

    int count() const;
    QStringList ids() const;

    /// 未命中返回 nullptr
    const Skill *find(const QString &id) const;

    QString root() const;
    SkillSource source() const;

    /// 扫描得到的全部 manifest（顺序与目录名升序一致）
    QList<SkillManifest> manifests() const;

private:
    QString m_root;
    SkillSource m_source = SkillSource::Bundled;
    std::vector<std::unique_ptr<Skill>> m_skills;
};
