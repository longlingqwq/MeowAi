#include "core/retriever/FullTextAssembler.h"

#include "core/retriever/ChunkSplitter.h"
#include "storage/LibraryStore.h"

#include <climits>

namespace {

/// 找相邻片段的最大可去重长度：从 upperBound 递减尝试「前块尾 == 后块头」
int overlapLength(const QString &prev, const QString &next, int upperBound)
{
    if (prev.isEmpty() || next.isEmpty() || upperBound <= 0) {
        return 0;
    }
    const int limit = qMin(upperBound, qMin(prev.size(), next.size()));
    for (int len = limit; len > 0; --len) {
        if (prev.endsWith(next.left(len))) {
            return len;
        }
    }
    return 0;
}

QString joinChunks(const QList<ChunkRecord> &chunks, int overlap, int *outTokenEst)
{
    if (chunks.isEmpty()) {
        if (outTokenEst) *outTokenEst = 0;
        return {};
    }

    QString text = chunks.first().text;
    for (int i = 1; i < chunks.size(); ++i) {
        const int skip = overlapLength(text, chunks[i].text, overlap);
        text.append(chunks[i].text.mid(skip));
    }

    if (outTokenEst) {
        *outTokenEst = estimateTokens(text);
    }
    return text;
}

} // namespace

FullTextAssembler::FullTextAssembler(LibraryStore *store)
    : m_store(store)
{
}

QString FullTextAssembler::assemble(qint64 fileId, int overlap, int *outTokenEst,
                                    QString *error) const
{
    return assembleRange(fileId, 0, INT_MAX, overlap, outTokenEst, error);
}

QString FullTextAssembler::assembleRange(qint64 fileId, int ordinalFrom, int ordinalTo, int overlap,
                                         int *outTokenEst, QString *error) const
{
    if (!m_store || !m_store->isAvailable()) {
        if (error) *error = QStringLiteral("数据库不可用");
        return {};
    }

    const QList<ChunkRecord> chunks =
        m_store->chunksInOrdinalRange(fileId, ordinalFrom, ordinalTo, error);
    return joinChunks(chunks, overlap, outTokenEst);
}
