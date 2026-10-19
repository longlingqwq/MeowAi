#include "core/retriever/NullRetriever.h"

QString NullRetriever::backendId() const
{
    return QStringLiteral("null");
}

QList<RetrievedChunk> NullRetriever::retrieve(const QString &query, int topK, QString *error)
{
    Q_UNUSED(query)
    Q_UNUSED(topK)
    if (error) {
        error->clear();
    }
    // 预留接口：阶段一不返回任何命中，也不视为错误
    return {};
}
