#pragma once

#include "core/retriever/DocumentRetriever.h"
#include "core/retriever/RetrievalConfig.h"
#include "providers/ChatTypes.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QVector>

class EmbeddingService;
class LibraryStore;
class VectorIndex;
struct EmbeddingResult;

/**
 * 资料库检索（模块 M5）：向量 + FTS5 关键词兜底 + 每文件上限 + 邻居扩展 + 阈值 + token 预算。
 *
 * 两种调用口径：
 *  - `retrieve()`（同步，继承 `DocumentRetriever`）：**只做关键词检索，绝不联网**；
 *    用于向量不可用时的兜底，也便于在没有网络依赖的地方复用。
 *  - `retrieveAsync()`（异步）：先用 embedding 向量化查询再做余弦检索；embedding 未配置 /
 *    失败 / 忙时按 `ftsFallback` 自动降级为关键词检索（`degraded = true`，绝不崩）。
 *
 * 检索顺序（顺序本身就是质量保证）：
 *  ① 候选召回（向量过阈值 / 关键词按词覆盖度）→ ② 每文件上限截断 → ③ 邻居扩展
 *  → ④ token 预算截断。每文件上限在扩展**之前**生效，否则大文件仍会占满预算。
 *
 * 隐私：本类不读 Key、不落盘、不写日志；`RetrievedChunk::sourcePath` 仅内部核对，
 * 注入 prompt 时由上层只取文件名 + 定位串 + 正文。
 */
class RagRetriever : public QObject, public DocumentRetriever
{
    Q_OBJECT

public:
    /// store / index / embedding 均不转移所有权；embedding 可为空（此时只能关键词检索）
    RagRetriever(LibraryStore *store, VectorIndex *index, EmbeddingService *embedding,
                 QObject *parent = nullptr);

    QString backendId() const override;

    /// 同步关键词检索（`topK` 覆盖配置里的 topK；不联网、不发请求）
    QList<RetrievedChunk> retrieve(const QString &query, int topK, QString *error) override;

    void setConfig(const RetrievalConfig &config);
    RetrievalConfig config() const;

    /// 挂载的小库；为空表示**不检索**（避免误把未挂载的库内容注入对话）
    void setMountedCollections(const QList<qint64> &collectionIds);
    QList<qint64> mountedCollections() const;

    /// 当前用于向量的「厂商 + 模型」；为空表示尚未建立向量
    void setEmbeddingModel(const QString &providerId, const QString &modelId);
    QString embeddingProviderId() const;
    QString embeddingModelId() const;

    bool busy() const;

    /// 异步检索；结果经 `finished` / `failed` 回传
    void retrieveAsync(const QString &query);
    /// 放弃本次结果（不中断 embedding 服务本身，避免影响正在进行的入库任务）
    void cancel();

signals:
    void finished(const RetrievalResult &result);
    void failed(const ChatError &error);

private:
    /// 挂载范围内的文件 → 所属小库（同一文件可属多库，取第一个挂载库用于来源展示）
    bool scopeFiles(QHash<qint64, qint64> *collectionOfFile, QString *error) const;
    void handleQueryEmbedded(const EmbeddingResult &result);
    void handleQueryEmbeddingFailed(const ChatError &error);

    /// 向量召回：过阈值、按分数降序、截断到 maxCandidates
    QList<RetrievedChunk> vectorCandidates(const QVector<float> &queryVector, int *hitCount,
                                           QString *error) const;
    /// 关键词召回：按词拆分后统计覆盖度（embedding 不可用时的兜底）
    QList<RetrievedChunk> keywordCandidates(const QString &query, int limit, QString *error) const;

    /// 每文件上限 + topK
    QList<RetrievedChunk> limitPerFile(const QList<RetrievedChunk> &candidates, int topK) const;
    /// 邻居扩展（保持「命中片段在前、前后邻居紧随」的阅读顺序）
    QList<RetrievedChunk> expandNeighbors(const QList<RetrievedChunk> &primary) const;
    /// token 预算截断；返回实际占用
    int applyTokenBudget(QList<RetrievedChunk> *chunks) const;

    void finishKeyword(const QString &query, const QString &note, const ChatError &cause);
    void finishWithResult(const QList<RetrievedChunk> &primary, int vectorHits, int keywordHits,
                          const QString &mode, bool degraded, const QString &note);
    void cleanupConnections();
    QString queryVectorUnavailableNote() const;

    LibraryStore *m_store = nullptr;
    VectorIndex *m_index = nullptr;
    EmbeddingService *m_embedding = nullptr;

    RetrievalConfig m_config;
    QList<qint64> m_collections;
    QString m_providerId;
    QString m_modelId;

    bool m_busy = false;
    QString m_query;
    QList<QMetaObject::Connection> m_connections;
};
