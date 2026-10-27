#pragma once

#include "core/skill/Skill.h"
#include "core/skill/SkillStateStore.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class SkillInstaller;
class SkillRegistry;

/**
 * 技能仓库门面（功能 #9）：内置 + 用户导入两个根目录合并成一份目录，
 * 并负责启用状态、全局模式与「本轮注入内容」的编排。
 *
 * 职责边界：
 *  - 只读 qrc 内置技能，**绝不改写**；删除只对导入技能开放；
 *  - 状态只落 skills_state.json（见 SkillStateStore），不碰数据库；
 *  - 注入预算与截断规则只在本类实现，ChatController 只负责插到正确位置。
 */
class SkillCatalog : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList skills READ skills NOTIFY skillsChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool manualMode READ manualMode NOTIFY modeChanged)
    Q_PROPERTY(int bundledCount READ bundledCount NOTIFY skillsChanged)
    Q_PROPERTY(int importedCount READ importedCount NOTIFY skillsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY skillsChanged)

public:
    /// 自动模式最多注入的技能数
    static constexpr int kMaxAutoSkills = 2;
    /// 技能注入总量上限（字符）
    static constexpr int kMaxInjectionChars = 4000;

    SkillCatalog(QString bundledRoot, QString userRoot, SkillStateStore *state,
                 QObject *parent = nullptr);
    ~SkillCatalog() override;

    /// 重新扫描两个根目录；内置优先，同 id 的导入技能不展示
    bool reload();

    QVariantList skills() const;
    /// 详情（含 skill.md 全文）；未命中返回空表
    Q_INVOKABLE QVariantMap detail(const QString &id) const;

    int bundledCount() const;
    int importedCount() const;
    QString mode() const;
    bool manualMode() const;
    void setMode(const QString &mode);
    QString statusMessage() const;

    Q_INVOKABLE QVariantMap importPackage(const QString &zipPath);
    Q_INVOKABLE QVariantMap removeSkill(const QString &id);
    Q_INVOKABLE bool setSkillEnabled(const QString &id, bool enabled);

    /// 本轮注入结果：text 为空表示无命中 / 无可用技能（调用方不得注入空内容）
    struct Injection
    {
        QString text;
        QStringList skillIds;
    };

    /**
     * 编排本轮注入：
     *  - 手动模式：用 manualIds（过滤禁用 / 不存在 / 去重，最多 2 个）；
     *  - 自动模式：对 userText 做触发词匹配（不区分大小写，中文按子串）。
     * 总量不超过 kMaxInjectionChars，超出直接截断且不追加任何提示。
     */
    Injection composeInjection(const QString &userText, const QStringList &manualIds) const;

    /// 自动模式命中顺序：触发词在输入中首次出现的位置升序，同位置按目录顺序
    QStringList matchAuto(const QString &userText) const;

    bool isEnabled(const QString &id) const;
    QString document(const QString &id) const;
    QStringList ids() const;
    SkillSource sourceOf(const QString &id) const;

signals:
    void skillsChanged();
    void modeChanged();

private:
    struct Entry
    {
        SkillManifest manifest;
        SkillSource source = SkillSource::Bundled;
        QString directory;
    };

    const Skill *findSkill(const QString &id) const;
    const Entry *findEntry(const QString &id) const;
    QStringList bundledIds() const;
    QStringList sanitizeManualIds(const QStringList &ids) const;
    static QString injectionBlock(const Entry &entry, const QString &document);

    QString m_bundledRoot;
    QString m_userRoot;
    SkillStateStore *m_state = nullptr;
    /// 内存态：启动时读一次，每次变更整份落盘（字段只有 mode + enabled）
    SkillStateData m_stateData;
    std::unique_ptr<SkillRegistry> m_bundled;
    std::unique_ptr<SkillRegistry> m_imported;
    std::unique_ptr<SkillInstaller> m_installer;
    /// 展示顺序：内置（id 升序）→ 导入（id 升序）
    QList<Entry> m_entries;
    QString m_statusMessage;
};
