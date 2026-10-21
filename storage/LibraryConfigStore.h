#pragma once

#include <QList>
#include <QString>

/**
 * 资料库配置存储（config.json 的 `library` 段）。
 *
 * 职责边界：
 *  - **只读写** `library` 段；ai / theme / personalization / web 等其他段一律保留不动；
 *  - **绝不保存任何密钥**（embedding / caption 的 Key 只走 `storage/SecureStore`）；
 *  - 写入采用「读现有 JSON → 只替换本段 → QSaveFile 原子写」，损坏文件以空对象重建、不崩溃。
 */

/// embedding 服务配置（可与对话厂商同源，也可单独配置一个专用服务）
struct LibraryEmbeddingConfig
{
    QString providerId;  ///< 复用厂商 id；为空表示尚未配置
    QString modelId;
    int dimensions = 0;  ///< 0 = 使用模型默认维度
    int batchSize = 16;  ///< 单次请求的文本条数（受厂商批量上限约束，M4 内再按厂商裁剪）
};

/// 图片入库描述（caption）使用的视觉模型配置
struct LibraryCaptionConfig
{
    QString providerId;
    QString modelId;
    int maxEdge = 1536;  ///< 图片长边压缩上限：显著降低视觉 token 消耗
    int maxTokens = 500;
    QString promptProfile = QStringLiteral("default");
};

struct LibraryConfigData
{
    bool enabled = false;
    bool consentGiven = false;  ///< 首次启用的一次性明确同意
    qint64 consentAt = 0;

    // 检索（三个可配置项直接决定行为差异）
    int topK = 6;
    int perFileLimit = 2;  ///< 每文件片段上限：防止单个大文件占满 Top-K
    int tokenBudget = 2000;
    double similarityThreshold = 0.30;
    int neighborRadius = 1;   ///< 邻居扩展半径（命中片段前后各带几段）
    bool ftsFallback = true;  ///< embedding 不可用时降级为关键词检索

    // 切分
    int chunkSize = 700;
    int chunkOverlap = 120;

    LibraryEmbeddingConfig embedding;
    LibraryCaptionConfig caption;
    QList<qint64> mountedCollections;  ///< 对话侧挂载的小库（重启保留）
};

class LibraryConfigStore
{
public:
    explicit LibraryConfigStore(QString configFile);

    /// 读取 `library` 段；文件缺失 / 损坏 / 段缺失一律回退默认值并写 error，**不视为致命错误**
    LibraryConfigData load(QString *error = nullptr) const;

    bool save(const LibraryConfigData &data, QString *error = nullptr) const;

private:
    QString m_configFile;
};
