#pragma once

#include "core/library/extract/ExtractorRegistry.h"
#include "storage/LibraryConfigStore.h"

#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>

class EmbeddingService;
class ImageCaptionService;
class LibraryStore;
class SqliteVectorIndex;

/**
 * 资料库索引器（模块 M7-a）。
 *
 * 职责：把「文件记录」变成「可检索的片段 + 向量」：
 *  1. 经 `ExtractorRegistry` 本地解析（不联网）；
 *  2. 图片：在**用户已同意**的前提下经 `ImageCaptionService` 生成描述文本；
 *  3. `ChunkSplitter` 切分并 `LibraryStore::replaceChunks` 落库；
 *  4. `EmbeddingService` 向量化 → `SqliteVectorIndex` 写向量。
 *
 * 降级口径（DoD 第 3 条，绝不崩、绝不丢用户文件）：
 *  - 未配置 embedding / 未同意外发 / 服务忙：片段照常入库，状态 `needs_embedding`；
 *  - 图片未配置视觉模型或未同意：状态 `pending`（重新配置后可重建）；
 *  - 扫描件等无文本层：`no_text_layer`；视频：`unsupported`；解析失败：`failed`。
 *
 * 队列 FIFO 逐条处理；同一时刻只有一个文件在途，避免把厂商限流打满。
 */
class LibraryIndexer : public QObject
{
    Q_OBJECT

public:
    LibraryIndexer(LibraryStore *store, SqliteVectorIndex *index, EmbeddingService *embedding,
                   QObject *parent = nullptr);

    /// 切分参数与同意开关来自 `LibraryConfigData`；每次配置变更后重新注入
    void setConfig(const LibraryConfigData &config);
    /// caption 服务由上层持有；为空表示未配置图片描述能力
    void setCaptionService(ImageCaptionService *caption);

    bool isAvailable() const;
    /// 有在途文件或排队文件
    bool isBusy() const;
    int queuedCount() const;

    void enqueueFile(qint64 fileId);
    void enqueueFiles(const QList<qint64> &fileIds);
    /// 清空队列并中断在途请求；在途文件不写失败状态（保持待索引）
    void cancel();

signals:
    void fileStarted(qint64 fileId);
    /// status 为最终落库状态；message 面向用户，不含密钥与正文
    void fileFinished(qint64 fileId, const QString &status, const QString &message);
    /// 一轮队列处理完毕
    void queueDrained(int indexed, int needsEmbedding, int failed);
    /// 实测 token 用量（供成本预估校准；usage 为 -1 时忽略）
    void usageRecorded(int embeddingTokens, int captionTokens);

private:
    enum class Phase
    {
        Idle,
        Extracting,
        Captioning,
        Embedding,
    };

    void processNext();
    void indexCurrent();
    void saveSegments(const QList<ExtractedSegment> &segments);
    void beginEmbedding();
    void finishFile(const QString &status, const QString &message);
    void onCaptionReady(qint64 fileId, int imageOrdinal, const QString &caption);
    void onCaptionFailed(qint64 fileId);
    void onEmbeddingFinished();
    void onEmbeddingFailed();
    void resetRound();

    LibraryStore *m_store = nullptr;
    SqliteVectorIndex *m_index = nullptr;
    EmbeddingService *m_embedding = nullptr;
    ImageCaptionService *m_caption = nullptr;
    ExtractorRegistry m_registry;
    LibraryConfigData m_config;

    QQueue<qint64> m_queue;
    qint64 m_currentId = 0;
    Phase m_phase = Phase::Idle;
    QList<qint64> m_chunkIds;
    QStringList m_chunkTexts;
    bool m_roundActive = false;
    int m_indexed = 0;
    int m_needsEmbedding = 0;
    int m_failed = 0;
};
