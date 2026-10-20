#pragma once

#include <QString>

/**
 * 联网检索配置存储（v0.2.3 新模块）。
 *
 * 职责边界（与 `storage/AiConfigStore` 同构）：
 *  - **只读写** config.json 的 `web` 段；ai / personalization / theme 等其他段一律保留不动；
 *  - **绝不保存搜索密钥**（密钥只走 storage/SecureStore），本类不感知任何明文密钥；
 *  - 写入采用「读现有 JSON → 只替换本段 → QSaveFile 原子写」，损坏文件不追加、不崩溃。
 */

/// `web` 段的完整内容
struct WebConfigData
{
    bool enabled = false;  ///< 「联网」开关记忆（重启后保持）
    QString endpoint;      ///< 自填搜索端点；可含 `{query}` 占位符
};

class WebConfigStore
{
public:
    explicit WebConfigStore(QString configFile);

    /// 读取 `web` 段；文件缺失 / 损坏 / 段缺失一律回退默认值并写 error，**不视为致命错误**
    WebConfigData load(QString *error = nullptr) const;

    bool save(const WebConfigData &data, QString *error = nullptr) const;

private:
    QString m_configFile;
};
