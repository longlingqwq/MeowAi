#include "core/retriever/RagRetriever.h"

#include "core/retriever/ChunkSplitter.h"
#include "core/retriever/EmbeddingService.h"
#include "core/retriever/VectorIndex.h"
#include "storage/LibraryStore.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

/// 文件名 / 定位串 + 正文；**不含本地路径**（路径只在内部核对，不进 prompt）
RetrievedChunk makeChunk(const ChunkRecord &chunk, const LibraryFileRecord &file, double score,
                         qint64 collectionId)
{
    RetrievedChunk hit;
    hit.chunkId = chunk.id;
    hit.fileId = chunk.fileId;
    hit.collectionId = collectionId;
    hit.fileName = file.displayName;
    hit.sourcePath = file.sourcePath;
    hit.text = chunk.text;
    hit.score = score;
    hit.ordinal = chunk.ordinal;
    hit.locKind = chunk.locKind;
    hit.locLabel = chunk.locLabel;
    hit.locPage = chunk.locPage;
    hit.locSheet = chunk.locSheet;
    hit.locRowFrom = chunk.locRowFrom;
    hit.locRowTo = chunk.locRowTo;
    hit.tokenEst = chunk.tokenEst > 0 ? chunk.tokenEst : estimateTokens(chunk.text);
    return hit;
}

double cosine(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty()) {
        return 0.0;
    }
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a.at(i)) * static_cast<double>(b.at(i));
        normA += static_cast<double>(a.at(i)) * static_cast<double>(a.at(i));
        normB += static_cast<double>(b.at(i)) * static_cast<double>(b.at(i));
    }
    if (normA <= 0.0 || normB <= 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

QStringList queryTerms(const QString &query)
{
    static const QRegularExpression separators(
        QStringLiteral("[\\s,，。．.！!？?；;、:：/\\\\|]+"));
    QStringList terms = query.split(separators, Qt::SkipEmptyParts);
    if (terms.isEmpty()) {
        terms.append(query.trimmed());
    }
    return terms;
}

ChatError makeError(ChatError::Code code, const QString &message)
{
    ChatError error;
    error.code = code;
    error.message = message;
    return error;
}

}  // namespace

RagRetriever::RagRetriever(LibraryStore *store, VectorIndex *index, EmbeddingService *embedding,
                           QObject *parent)
    : QObject(parent), m_store(store), m_index(index), m_embedding(embedding)
{
}

QString RagRetriever::backendId() const
{
    return QStringLiteral("rag-sqlite");
}

void RagRetriever::setConfig(const RetrievalConfig &config)
{
    m_config = config;
    m_config.topK = qMax(1, m_config.topK);
    m_config.perFileLimit = qMax(1, m_config.perFileLimit);
    m_config.neighborRadius = qMax(0, m_config.neighborRadius);
    m_config.maxCandidates = qMax(m_config.topK, m_config.maxCandidates);
}

RetrievalConfig RagRetriever::config() const
{
    return m_config;
}

void RagRetriever::setMountedCollections(const QList<qint64> &collectionIds)
{
    m_collections = collectionIds;
}

QList<qint64> RagRetriever::mountedCollections() const
{
    return m_collections;
}

void RagRetriever::setEmbeddingModel(const QString &providerId, const QString &modelId)
{
    m_providerId = providerId.trimmed();
    m_modelId = modelId.trimmed();
}

QString RagRetriever::embeddingProviderId() const
{
    return m_providerId;
}

QString RagRetriever::embeddingModelId() const
{
    return m_modelId;
}

bool RagRetriever::busy() const
{
    return m_busy;
}

QString RagRetriever::queryVectorUnavailableNote() const
{
    if (!m_embedding) {
        return QStringLiteral("未接入 embedding 服务，已降级为关键词检索");
    }
    if (m_providerId.isEmpty() || m_modelId.isEmpty()) {
        return QStringLiteral("尚未建立向量索引（未配置 embedding 模型），已降级为关键词检索");
    }
    if (!m_embedding->isReady()) {
        return QStringLiteral("embedding 服务未配置完整，已降级为关键词检索");
    }
    return QStringLiteral("向量检索不可用，已降级为关键词检索");
}

bool RagRetriever::scopeFiles(QHash<qint64, qint64> *collectionOfFile, QString *error) const
{
    if (!m_store) {
        if (error) {
            *error = QStringLiteral("资料库存储不可用");
        }
        return false;
    }
    for (qint64 collectionId : m_collections) {
        QString storeError;
        const QList<LibraryFileRecord> files = m_store->filesInCollection(collectionId, &storeError);
        if (!storeError.isEmpty()) {
            if (error) {
                *error = storeError;
            }
            return false;
        }
        for (const LibraryFileRecord &file : files) {
            if (!collectionOfFile->contains(file.id)) {
                collectionOfFile->insert(file.id, collectionId);
            }
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

QList<RetrievedChunk> RagRetriever::retrieve(const QString &query, int topK, QString *error)
{
    if (error) {
        error->clear();
    }
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        if (error) {
            *error = QStringLiteral("检索词为空");
        }
        return {};
    }
    // 未挂载小库 → 不检索（避免把未挂载内容注入对话）
    if (m_collections.isEmpty()) {
        return {};
    }

    QString keywordError;
    const QList<RetrievedChunk> candidates =
        keywordCandidates(needle, qMax(topK, 1) * 4, &keywordError);
    if (!keywordError.isEmpty()) {
        if (error) {
            *error = keywordError;
        }
        return {};
    }
    QList<RetrievedChunk> primary = limitPerFile(candidates, qMax(topK, 1));
    primary = expandNeighbors(primary);
    applyTokenBudget(&primary);
    return primary;
}

QList<RetrievedChunk> RagRetriever::keywordCandidates(const QString &query, int limit,
                                                       QString *error) const
{
    if (!m_store || m_collections.isEmpty()) {
        return {};
    }

    const QStringList terms = queryTerms(query);
    QHash<qint64, int> hitCounts;
    QString firstError;
    for (const QString &term : terms) {
        QString searchError;
        const QList<qint64> ids =
            m_store->searchChunkIds(term, m_collections, qMax(limit, 10), &searchError);
        if (!searchError.isEmpty()) {
            if (firstError.isEmpty()) {
                firstError = searchError;
            }
            continue;
        }
        for (qint64 id : ids) {
            hitCounts[id] += 1;
        }
    }
    if (hitCounts.isEmpty()) {
        if (!firstError.isEmpty() && error) {
            *error = firstError;
        } else if (error) {
            error->clear();
        }
        return {};
    }

    QList<QPair<qint64, int>> ranked;
    ranked.reserve(hitCounts.size());
    for (auto it = hitCounts.constBegin(); it != hitCounts.constEnd(); ++it) {
        ranked.append({it.key(), it.value()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const QPair<qint64, int> &a,
                                               const QPair<qint64, int> &b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });
    if (ranked.size() > limit) {
        ranked = ranked.mid(0, limit);
    }

    QList<qint64> ids;
    ids.reserve(ranked.size());
    for (const auto &entry : ranked) {
        ids.append(entry.first);
    }

    QString storeError;
    const QList<ChunkRecord> chunks = m_store->chunksByIds(ids, &storeError);
    if (!storeError.isEmpty()) {
        if (error) {
            *error = storeError;
        }
        return {};
    }
    QHash<qint64, ChunkRecord> chunkById;
    for (const ChunkRecord &chunk : chunks) {
        chunkById.insert(chunk.id, chunk);
    }

    QHash<qint64, qint64> collectionOfFile;
    QString scopeError;
    if (!scopeFiles(&collectionOfFile, &scopeError)) {
        if (error) {
            *error = scopeError;
        }
        return {};
    }

    QHash<qint64, LibraryFileRecord> fileById;
    QList<RetrievedChunk> result;
    for (const auto &entry : ranked) {
        if (!chunkById.contains(entry.first)) {
            continue;
        }
        const ChunkRecord chunk = chunkById.value(entry.first);
        if (!fileById.contains(chunk.fileId)) {
            LibraryFileRecord file;
            QString fileError;
            if (!m_store->fileById(chunk.fileId, &file, &fileError) || !fileError.isEmpty()) {
                continue;  // 文件记录缺失：跳过该片段，不影响其余结果
            }
            fileById.insert(chunk.fileId, file);
        }
        // 关键词覆盖度映射到 [0.5, 1.0]：余弦阈值只作用于向量分，不做二次误杀
        const double coverage = static_cast<double>(entry.second) / terms.size();
        result.append(makeChunk(chunk, fileById.value(chunk.fileId),
                                0.5 + 0.5 * std::min(1.0, coverage),
                                collectionOfFile.value(chunk.fileId, 0)));
    }
    if (error) {
        error->clear();
    }
    return result;
}

QList<RetrievedChunk> RagRetriever::vectorCandidates(const QVector<float> &queryVector,
                                                      int *hitCount, QString *error) const
{
    if (hitCount) {
        *hitCount = 0;
    }
    if (error) {
        error->clear();
    }
    if (!m_store || !m_index || m_providerId.isEmpty() || m_modelId.isEmpty()
        || m_collections.isEmpty() || queryVector.isEmpty()) {
        return {};
    }

    QHash<qint64, qint64> collectionOfFile;
    QString scopeError;
    if (!scopeFiles(&collectionOfFile, &scopeError)) {
        if (error) {
            *error = scopeError;
        }
        return {};
    }

    QString indexError;
    const QList<StoredVector> stored = m_index->vectors(m_providerId, m_modelId, &indexError);
    if (!indexError.isEmpty()) {
        if (error) {
            *error = indexError;
        }
        return {};
    }

    struct Candidate
    {
        qint64 chunkId = 0;
        qint64 fileId = 0;
        double score = 0.0;
    };
    QList<Candidate> candidates;
    for (const StoredVector &vector : stored) {
        if (!collectionOfFile.contains(vector.fileId)) {
            continue;  // 不在挂载范围内
        }
        const double score = cosine(queryVector, vector.values);
        if (score < m_config.similarityThreshold) {
            continue;  // 相似度阈值：低分片段直接丢弃
        }
        candidates.append({vector.chunkId, vector.fileId, score});
    }
    if (hitCount) {
        *hitCount = candidates.size();
    }
    if (candidates.isEmpty()) {
        return {};
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });
    if (candidates.size() > m_config.maxCandidates) {
        candidates = candidates.mid(0, m_config.maxCandidates);
    }

    QList<qint64> ids;
    for (const Candidate &candidate : candidates) {
        ids.append(candidate.chunkId);
    }

    const QList<ChunkRecord> chunks = m_store->chunksByIds(ids, &indexError);
    if (!indexError.isEmpty()) {
        if (error) {
            *error = indexError;
        }
        return {};
    }
    QHash<qint64, ChunkRecord> chunkById;
    for (const ChunkRecord &chunk : chunks) {
        chunkById.insert(chunk.id, chunk);
    }

    QHash<qint64, LibraryFileRecord> files;
    QList<RetrievedChunk> result;
    for (const Candidate &candidate : candidates) {
        if (!chunkById.contains(candidate.chunkId)) {
            continue;
        }
        const ChunkRecord chunk = chunkById.value(candidate.chunkId);
        if (!files.contains(chunk.fileId)) {
            LibraryFileRecord file;
            QString fileError;
            if (!m_store->fileById(chunk.fileId, &file, &fileError) || !fileError.isEmpty()) {
                continue;
            }
            files.insert(chunk.fileId, file);
        }
        result.append(makeChunk(chunk, files.value(chunk.fileId), candidate.score,
                                collectionOfFile.value(chunk.fileId, 0)));
    }
    return result;
}

QList<RetrievedChunk> RagRetriever::limitPerFile(const QList<RetrievedChunk> &candidates,
                                                  int topK) const
{
    QList<RetrievedChunk> result;
    QHash<qint64, int> perFile;
    for (const RetrievedChunk &candidate : candidates) {
        if (result.size() >= topK) {
            break;
        }
        int &count = perFile[candidate.fileId];
        if (count >= m_config.perFileLimit) {
            continue;  // 每文件片段上限：强制多样性，防止一个大文件占满 Top-K
        }
        ++count;
        result.append(candidate);
    }
    return result;
}

QList<RetrievedChunk> RagRetriever::expandNeighbors(const QList<RetrievedChunk> &primary) const
{
    if (m_config.neighborRadius <= 0 || !m_store || primary.isEmpty()) {
        return primary;
    }

    QList<RetrievedChunk> result;
    QSet<qint64> seen;
    for (const RetrievedChunk &hit : primary) {
        QString storeError;
        const QList<ChunkRecord> window = m_store->chunksInOrdinalRange(
            hit.fileId, hit.ordinal - m_config.neighborRadius,
            hit.ordinal + m_config.neighborRadius, &storeError);
        if (!storeError.isEmpty()) {
            // 邻居扩展失败不影响已命中的片段
            if (!seen.contains(hit.chunkId)) {
                seen.insert(hit.chunkId);
                result.append(hit);
            }
            continue;
        }
        for (const ChunkRecord &chunk : window) {
            if (seen.contains(chunk.id)) {
                continue;
            }
            if (chunk.id == hit.chunkId) {
                seen.insert(chunk.id);
                result.append(hit);  // 命中片段保留原分数与来源
                continue;
            }
            RetrievedChunk neighbor = hit;
            neighbor.chunkId = chunk.id;
            neighbor.ordinal = chunk.ordinal;
            neighbor.text = chunk.text;
            neighbor.locKind = chunk.locKind;
            neighbor.locLabel = chunk.locLabel;
            neighbor.locPage = chunk.locPage;
            neighbor.locSheet = chunk.locSheet;
            neighbor.locRowFrom = chunk.locRowFrom;
            neighbor.locRowTo = chunk.locRowTo;
            neighbor.tokenEst = chunk.tokenEst > 0 ? chunk.tokenEst : estimateTokens(chunk.text);
            neighbor.score = hit.score * 0.85;  // 邻居不是直接命中，分数略低（仅用于展示排序）
            seen.insert(chunk.id);
            result.append(neighbor);
        }
    }
    return result;
}

int RagRetriever::applyTokenBudget(QList<RetrievedChunk> *chunks) const
{
    if (!chunks) {
        return 0;
    }
    const int budget = m_config.tokenBudget;
    if (budget <= 0) {
        int used = 0;
        for (const RetrievedChunk &chunk : *chunks) {
            used += chunk.tokenEst;
        }
        return used;
    }

    QList<RetrievedChunk> kept;
    int used = 0;
    for (const RetrievedChunk &chunk : *chunks) {
        const int cost = chunk.tokenEst > 0 ? chunk.tokenEst : estimateTokens(chunk.text);
        if (used + cost > budget) {
            // 一条都放不下时保留首条：宁可略超预算，也不给出「空检索」的错误印象
            if (kept.isEmpty()) {
                kept.append(chunk);
                used += cost;
            }
            break;
        }
        kept.append(chunk);
        used += cost;
    }
    *chunks = kept;
    return used;
}

void RagRetriever::retrieveAsync(const QString &query)
{
    if (m_busy) {
        return;  // 单实例同一时刻只承载一次检索，避免结果串台
    }

    const QString needle = query.trimmed();
    m_query = needle;
    m_busy = true;

    if (needle.isEmpty()) {
        RetrievalResult result;
        result.mode = QStringLiteral("keyword");
        result.note = QStringLiteral("检索词为空");
        m_busy = false;
        emit finished(result);
        return;
    }
    if (m_collections.isEmpty()) {
        RetrievalResult result;
        result.mode = QStringLiteral("keyword");
        result.note = QStringLiteral("未挂载小库，未进行资料库检索");
        m_busy = false;
        emit finished(result);
        return;
    }

    const bool vectorPossible = m_embedding && m_embedding->isReady()
                                && !m_providerId.isEmpty() && !m_modelId.isEmpty();
    if (!vectorPossible) {
        finishKeyword(needle, queryVectorUnavailableNote(),
                      makeError(ChatError::NotConfigured, queryVectorUnavailableNote()));
        return;
    }
    if (m_embedding->busy()) {
        const QString note = QStringLiteral("向量化服务正忙，已降级为关键词检索");
        finishKeyword(needle, note, makeError(ChatError::Unknown, note));
        return;
    }

    m_connections.append(connect(m_embedding, &EmbeddingService::finished, this,
                                 [this](const EmbeddingResult &result) {
                                     handleQueryEmbedded(result);
                                 }));
    m_connections.append(connect(m_embedding, &EmbeddingService::failed, this,
                                 [this](const ChatError &error) {
                                     handleQueryEmbeddingFailed(error);
                                 }));
    m_embedding->embed({needle});
}

void RagRetriever::handleQueryEmbedded(const EmbeddingResult &result)
{
    if (!m_busy) {
        return;
    }
    if (!result.ok || result.vectors.size() != 1 || result.vectors.first().isEmpty()) {
        const ChatError error = makeError(ChatError::InvalidResponse,
                                          QStringLiteral("查询向量为空，已降级为关键词检索"));
        finishKeyword(m_query, error.message, error);
        return;
    }

    int vectorHits = 0;
    QString vectorError;
    QList<RetrievedChunk> candidates = vectorCandidates(result.vectors.first(), &vectorHits,
                                                        &vectorError);
    if (!vectorError.isEmpty()) {
        finishKeyword(m_query, QStringLiteral("向量检索失败（%1），已降级为关键词检索")
                                   .arg(vectorError),
                      makeError(ChatError::Unknown, vectorError));
        return;
    }

    QList<RetrievedChunk> primary = limitPerFile(candidates, m_config.topK);

    int keywordHits = 0;
    QString mode = QStringLiteral("vector");
    if (m_config.ftsFallback && primary.size() < m_config.topK) {
        QString keywordError;
        const QList<RetrievedChunk> keyword =
            keywordCandidates(m_query, m_config.topK * 4, &keywordError);
        keywordHits = keyword.size();

        QSet<qint64> existing;
        QHash<qint64, int> perFile;
        for (const RetrievedChunk &chunk : primary) {
            existing.insert(chunk.chunkId);
            perFile[chunk.fileId] += 1;
        }
        bool merged = false;
        for (const RetrievedChunk &chunk : keyword) {
            if (primary.size() >= m_config.topK) {
                break;
            }
            if (existing.contains(chunk.chunkId)) {
                continue;
            }
            if (perFile.value(chunk.fileId) >= m_config.perFileLimit) {
                continue;
            }
            primary.append(chunk);
            existing.insert(chunk.chunkId);
            perFile[chunk.fileId] += 1;
            merged = true;
        }
        if (merged) {
            mode = QStringLiteral("hybrid");
        }
    }

    finishWithResult(primary, vectorHits, keywordHits, mode, false, QString());
}

void RagRetriever::handleQueryEmbeddingFailed(const ChatError &error)
{
    if (!m_busy) {
        return;
    }
    if (error.code == ChatError::Cancelled) {
        m_busy = false;
        cleanupConnections();
        return;
    }
    finishKeyword(m_query, QStringLiteral("查询向量化失败（%1），已降级为关键词检索")
                               .arg(error.message),
                  error);
}

void RagRetriever::finishKeyword(const QString &query, const QString &note,
                                  const ChatError &cause)
{
    if (!m_config.ftsFallback) {
        m_busy = false;
        cleanupConnections();
        emit failed(cause);
        return;
    }

    QString keywordError;
    const QList<RetrievedChunk> candidates =
        keywordCandidates(query, m_config.topK * 4, &keywordError);
    if (!keywordError.isEmpty()) {
        m_busy = false;
        cleanupConnections();
        emit failed(makeError(ChatError::Unknown, keywordError));
        return;
    }
    const QList<RetrievedChunk> primary = limitPerFile(candidates, m_config.topK);
    finishWithResult(primary, 0, candidates.size(), QStringLiteral("keyword"), true, note);
}

void RagRetriever::finishWithResult(const QList<RetrievedChunk> &primary, int vectorHits,
                                    int keywordHits, const QString &mode, bool degraded,
                                    const QString &note)
{
    RetrievalResult result;
    result.vectorHits = vectorHits;
    result.keywordHits = keywordHits;
    result.mode = mode;
    result.degraded = degraded;
    result.note = note;

    QList<RetrievedChunk> chunks = expandNeighbors(primary);
    result.tokenEst = applyTokenBudget(&chunks);
    result.chunks = chunks;

    m_busy = false;
    m_query.clear();
    cleanupConnections();
    emit finished(result);
}

void RagRetriever::cancel()
{
    if (!m_busy) {
        return;
    }
    m_busy = false;
    m_query.clear();
    cleanupConnections();
}

void RagRetriever::cleanupConnections()
{
    for (const QMetaObject::Connection &connection : m_connections) {
        disconnect(connection);
    }
    m_connections.clear();
}
