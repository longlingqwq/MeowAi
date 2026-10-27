#pragma once

#include <QString>
#include <QStringList>

/**
 * 技能包导入（功能 #9）。
 *
 * 输入：用户选择的 zip；输出：装到 <userRoot>/<id>/ 的成品目录。
 * 全流程在临时目录完成，任何一步失败都清理临时目录，不留半成品。
 *
 * 安全校验（缺一不可，失败即拒绝并给出中文原因）：
 *  1. 包体积 ≤ 20 MB；
 *  2. 解压后总体积 ≤ 100 MB（按中央目录声明值累加，写入时再复核一次）；
 *  3. 条目数 ≤ 2000（防 zip 炸弹）；
 *  4. 条目名不得为绝对路径、不得含盘符、不得出现 ".." 段、不得是符号链接；
 *  5. manifest.json 必须唯一，字段口径走 SkillManifestRules；
 *  6. id 只含字母 / 数字 / 连字符，且不得与内置技能冲突；
 *  7. skill.md 必须存在且非空；
 *  8. triggers 必须是非空字符串数组（自动模式的硬性前提）。
 */
struct SkillImportResult
{
    bool ok = false;
    QString skillId;
    /// 面向用户的结论（成功 / 失败都直接展示，不再二次拼装）
    QString message;
    QStringList warnings;
};

class SkillInstaller
{
public:
    static constexpr qint64 kDefaultMaxPackageBytes = 20LL * 1024 * 1024;
    static constexpr qint64 kDefaultMaxExtractedBytes = 100LL * 1024 * 1024;
    static constexpr int kMaxEntries = 2000;

    explicit SkillInstaller(QString userRoot,
                            qint64 maxPackageBytes = kDefaultMaxPackageBytes,
                            qint64 maxExtractedBytes = kDefaultMaxExtractedBytes);

    /// bundledIds：内置技能 id 集合，用于「同 id 冲突」判定
    SkillImportResult install(const QString &zipPath, const QStringList &bundledIds) const;

    QString userRoot() const;

    /// 面向用户的限制说明（失败提示里原样引用，避免两处文案漂移）
    static QString limitsText();

    /// 条目名安全判定（单独暴露便于单元测试直接覆盖目录穿越用例）
    static bool isSafeEntryName(const QString &entryName);

private:
    QString m_userRoot;
    qint64 m_maxPackageBytes = kDefaultMaxPackageBytes;
    qint64 m_maxExtractedBytes = kDefaultMaxExtractedBytes;
};
