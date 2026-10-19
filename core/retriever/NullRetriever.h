#pragma once

#include "core/retriever/DocumentRetriever.h"

/**
 * 检索接口的空实现占位：始终返回空结果且不报错。
 * 阶段一不实现检索算法，仅保证调用链可跑通、不崩溃。
 */
class NullRetriever : public DocumentRetriever
{
public:
    QString backendId() const override;
    QList<RetrievedChunk> retrieve(const QString &query, int topK, QString *error) override;
};
