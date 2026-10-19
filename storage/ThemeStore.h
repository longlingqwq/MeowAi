#pragma once

#include <QString>

/**
 * 主题选择存储（storage 层，纯文件读写）。
 *
 * 与个性化配置**分开**：本类只读写 `config.json` 中的 `theme` 段，
 * 采用「读现有 JSON → 只替换 theme 键 → 原子写回」，保留其他模块（含
 * `personalization`）写入的键，避免互相覆盖。
 */
class ThemeStore
{
public:
    explicit ThemeStore(QString configFile);

    /// 读取已选主题 id；文件缺失 / 损坏 / 未设置时返回空串并写 error，绝不抛异常
    QString loadPresetId(QString *error) const;

    /// 只替换 theme 段后原子写回；失败返回 false 并写 error
    bool savePresetId(const QString &presetId, QString *error) const;

private:
    QString m_configFile;
};
