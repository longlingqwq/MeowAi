#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

/**
 * AI 接入配置存储（功能 #3）。
 *
 * 职责边界：
 *  - **只读写** config.json 的 `ai` 段；personalization / theme 等其他段一律保留不动；
 *  - **绝不保存 API Key**（Key 只走 storage/SecureStore），本类不感知任何明文密钥；
 *  - 写入采用“读现有 JSON → 只替换本段 → QSaveFile 原子写”，损坏文件不追加、不崩溃。
 */

/// `ai` 段的完整内容
struct AiConfigData
{
    QString providerId;  ///< 当前厂商 id（catalog 中的 id）
    QString modelId;     ///< 当前模型 id；自定义厂商时即用户手填的模型名

    /// 思考档位记忆：key = "providerId.modelId" -> off / low / medium / high
    QHash<QString, QString> thinkingLevels;
    /// 高级原生参数覆盖：key = "providerId.modelId" -> 原生片段（存在即优先于档位映射）
    QHash<QString, QJsonObject> nativeOverrides;
    /// 自定义厂商的 API 链接（可手填，其余厂商由 catalog 提供）
    QString customBaseUrl;
};

class AiConfigStore
{
public:
    explicit AiConfigStore(QString configFile);

    /// 读取 `ai` 段；文件缺失 / 损坏 / 段缺失一律回退默认值并写 error，**不视为致命错误**
    AiConfigData load(QString *error = nullptr) const;

    bool save(const AiConfigData &data, QString *error = nullptr) const;

    /// 「厂商 + 模型」记忆键：跨厂商、跨模型互不干扰
    static QString selectionKey(const QString &providerId, const QString &modelId);

private:
    QString m_configFile;
};
