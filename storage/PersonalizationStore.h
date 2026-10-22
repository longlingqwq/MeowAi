#pragma once

#include <QString>

/**
 * 个性化配置数据（功能 #11）。
 *
 * **模块边界（重要）**：本结构只描述个性化设置，且只落在 `config.json` 的
 * `personalization` 段；API Key 等敏感数据仍只存于 `SecureStore` 保险库，
 * **绝不写入本配置**，避免污染安全模块语义。
 *
 * 资源路径一律以「相对 AppData 根目录」的形式保存（如 `assets/avatars/a.png`）：
 * 配置可移植、不把用户绝对路径写进文件，运行期再由 ThemeManager 解析为绝对路径。
 */
struct PersonalizationConfig
{
    QString fontFamily;             ///< 字体族名（系统字体，或导入字体注册后得到的族名）
    QString fontSource;             ///< 导入字体文件的相对路径；为空表示使用系统字体
    QString backgroundSource;       ///< 背景图相对路径；为空表示无自定义背景
    QString avatarSource;           ///< 头像相对路径；为空表示无自定义头像
    QString splashSource;           ///< 开屏视频相对路径；为空表示无自定义开屏
    bool splashEnabled = true;      ///< 是否在启动时播放开屏（内置开屏默认开启）
    bool splashSoundEnabled = true; ///< 开屏视频是否播放音效（视频自带音轨）
};

/**
 * 个性化配置存储（storage 层，纯文件读写）。
 *
 * 只做一件事：读写 `config.json` 中的 `personalization` 段。
 * 写入采用「读现有 JSON → 只替换 personalization 键 → 原子写回」，
 * **保留其他模块（未来模型配置等）写入的键**，避免互相覆盖。
 */
class PersonalizationStore
{
public:
    explicit PersonalizationStore(QString configFile);

    /// 读取个性化段；文件缺失或损坏时返回默认值并写入 error，绝不抛异常
    PersonalizationConfig load(QString *error) const;

    /// 只替换 personalization 段后原子写回；失败返回 false 并写入 error
    bool save(const PersonalizationConfig &config, QString *error) const;

    /// 相对路径白名单校验：拒绝绝对路径、盘符、`~` 与路径穿越（`..`）
    static bool isSafeRelativePath(const QString &relativePath);

private:
    QString m_configFile;
};
