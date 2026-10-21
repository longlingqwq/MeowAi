#pragma once

#include <QList>
#include <QString>

/**
 * 一次检索命中的片段。
 *
 * 除文本与分数外，**必须携带来源定位元数据**：用户核对「这句话出自哪个文件的第几页 /
 * 哪个工作表第几行」是资料库功能的底线（DoD 第 6 条），因此这里同时给出文件标识与
 * `chunks` 表中的定位字段。
 */
struct RetrievedChunk
{
    qint64 chunkId = 0;
    qint64 fileId = 0;
    qint64 collectionId = 0;  ///< 命中所属的挂载小库（0 = 未知 / 未挂载）
    QString fileName;         ///< 展示名（文件列表里的名字）
    QString sourcePath;       ///< 本地路径，仅供内部核对，不注入 prompt
    QString text;
    double score = 0.0;

    int ordinal = 0;  ///< 文件内片段序号（邻居扩展 / 排序用）
    QString locKind;
    QString locLabel;  ///< 人可读定位串：「第 3 页」「Sheet1 第 12-18 行」「图片 #2」
    int locPage = 0;
    QString locSheet;
    int locRowFrom = 0;
    int locRowTo = 0;
    int tokenEst = 0;
};

/**
 * 资料库检索接口（功能 #1）。
 *
 * 本接口是**同步口径**、且实现**不得联网**（`RagRetriever` 的同步实现只做关键词检索）；
 * 需要向量的异步检索由 `RagRetriever::retrieveAsync()` 提供，避免把网络塞进同步接口。
 */
class DocumentRetriever
{
public:
    virtual ~DocumentRetriever() = default;

    virtual QString backendId() const = 0;

    /// 失败时返回空列表并写入 error；实现不得抛出异常
    virtual QList<RetrievedChunk> retrieve(const QString &query, int topK, QString *error) = 0;
};
