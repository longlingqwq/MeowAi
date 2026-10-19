#pragma once

#include <QList>
#include <QString>

/// 一次检索命中的片段
struct RetrievedChunk
{
    QString sourcePath;
    QString text;
    double score = 0.0;
};

/**
 * 资料库检索接口（功能 #1）——本阶段仅预留，不实现任何检索算法。
 *
 * 待定项（关键词 / 向量）确认后，新增实现类替换 NullRetriever 即可，
 * 上层调用方无需改动（见 docs/PROJECT_CHARTER.md D10）。
 */
class DocumentRetriever
{
public:
    virtual ~DocumentRetriever() = default;

    virtual QString backendId() const = 0;

    /// 失败时返回空列表并写入 error；实现不得抛出异常
    virtual QList<RetrievedChunk> retrieve(const QString &query, int topK, QString *error) = 0;
};
