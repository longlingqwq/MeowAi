#pragma once

#include <QHash>
#include <QString>

/**
 * 技能启用状态与全局模式（功能 #9）。
 *
 * 落盘位置：%APPDATA%/MeowAi/skills_state.json —— **不新增数据库表**，
 * 避免与 M18 的 v5 迁移冲突。文件结构：
 * {
 *   "version": 1,
 *   "mode": "auto",                    // auto / manual
 *   "enabled": { "example": true }     // 缺省视为启用
 * }
 *
 * 文件缺失、损坏、字段类型不对都只回退默认（全部启用 + 自动模式）并写入 error，
 * 绝不抛异常、不阻断启动。
 */
struct SkillStateData
{
    QString mode = QStringLiteral("auto");
    QHash<QString, bool> enabled;

    /// 未记录的技能默认启用
    bool isEnabled(const QString &id) const { return enabled.value(id, true); }
};

class SkillStateStore
{
public:
    explicit SkillStateStore(QString filePath);

    SkillStateData load(QString *error = nullptr) const;

    /// 原子写入（QSaveFile）；失败写 error 并返回 false，调用方据此回滚内存态
    bool save(const SkillStateData &data, QString *error = nullptr) const;

    QString filePath() const;

    static bool isValidMode(const QString &mode);
    static QString autoMode();
    static QString manualMode();

private:
    QString m_filePath;
};
