#pragma once

#include <QList>
#include <QString>

#include "core/retriever/DocumentRetriever.h"
#include "core/retriever/InjectionPlanner.h"

struct LibraryConfigData;
class DocumentStore;

/// 单个相关文件的注入结果（供 ChatController 组装 prompt 与 UI 展示）
struct DocumentInjectionResult
{
    qint64 fileId = 0;
    qint64 collectionId = 0;
    QString fileName;
    double fileScore = 0.0;              ///< 文件聚合得分
    InjectionLevel level = InjectionLevel::None;
    QString content;                     ///< 注入正文（空表示未注入）
    int tokenEst = 0;
    bool truncated = false;
    QString trace;                       ///< 降级链路说明（中文）
    QString sectionSource;               ///< 章节来源类型（page_approx 表示近似切分）
};

/**
 * 文件级召回与注入编排。
 *
 * 职责边界：**只做文件级整理与注入决策，不做检索**。
 * 片段候选由 `RagRetriever::retrieveAsync()`（向量优先 + FTS 兜底）提供，
 * 本类据此完成：按文件聚合打分 → 阈值过滤 → 逐文件三级注入规划 → 全局预算截断。
 *
 * 无状态、无 IO、不持有检索器，便于单测与复用。
 */
class FileLevelRetriever
{
public:
    FileLevelRetriever(InjectionPlanner *planner, DocumentStore *docStore);

    /**
     * @param chunks         RagRetriever 产出的片段候选（已排序、已做每文件上限）
     * @param config         资料库配置（预算、聚合权重、overlap 等）
     * @param contextWindow  模型上下文窗口；0 或负数表示未知（回退保守值）
     * @param historyTokens  历史消息 token 估算
     * @param degradeNote    可选输出：预算降级原因（中文，空表示未降级）
     */
    QList<DocumentInjectionResult> planInjection(const QList<RetrievedChunk> &chunks,
                                                 const LibraryConfigData &config,
                                                 int contextWindow,
                                                 int historyTokens,
                                                 QString *degradeNote) const;

private:
    struct FileScore
    {
        qint64 fileId = 0;
        qint64 collectionId = 0;
        QString fileName;
        double maxScore = 0.0;
        int hitCount = 0;
        double aggregateScore = 0.0;
        QList<ChunkScore> chunkScores;
    };

    /// 按文件聚合：max(片段分) + 命中数 × weight，并按阈值过滤、降序排列
    QList<FileScore> aggregateByFile(const QList<RetrievedChunk> &chunks, double weight,
                                      double threshold) const;

    InjectionPlanner *m_planner = nullptr;
    DocumentStore *m_docStore = nullptr;
};
