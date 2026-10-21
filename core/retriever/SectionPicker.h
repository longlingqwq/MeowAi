#pragma once

#include <QList>
#include <QPair>
#include <QString>

class DocumentStore;

/**
 * SectionPicker：章节识别 + 按预算选章。
 *
 * 前提：documents/sections 元数据已由 LibraryIndexer 填充。
 * 章节识别由 LibraryIndexer 在索引完成时做，本类只负责"给定章节结构 + 给定片段相似度，
 * 按章节内最高相似度排序，依次取章直到预算用尽"。
 */

/// 某个 chunk 的相似度输入（由 FileLevelRetriever 提供）
struct ChunkScore
{
    int ordinal = 0;
    double score = 0.0;
};

/// 选中的章节 + 该章节对应的 chunk 范围 + token 估算
struct SelectedSection
{
    qint64 sectionId = 0;
    QString title;
    int chunkStart = 0;
    int chunkEnd = 0;
    int tokenEst = 0;
};

class SectionPicker
{
public:
    explicit SectionPicker(DocumentStore *docStore);

    /**
     * 按预算选章。
     *
     * @param fileId         文件 id
     * @param chunkScores    本文件命中的相似度分数列表（由 RagRetriever 提供）
     * @param budgetTokens   可用预算（token）
     * @param overlap        ChunkSplitter 的 overlap 配置，用于最终拼接时去重
     * @param outReason      可选输出：选章降级原因（"无章节结构"/"预算不足"等）
     * @return 选中的章节列表（可能为空）
     */
    QList<SelectedSection> pick(qint64 fileId,
                                const QList<ChunkScore> &chunkScores,
                                int budgetTokens,
                                int overlap,
                                QString *outReason) const;

private:
    DocumentStore *m_docStore = nullptr;
};
