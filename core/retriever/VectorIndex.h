#pragma once

#include <QList>
#include <QString>
#include <QVector>

/**
 * 一条已落库的向量（`chunk_vectors` 行 + 所属文件），供 M5 在**内存里**算余弦相似度。
 * 当前规模（小圈子共享的资料库）远小于需要专用向量库的量级，故先走 SQLite BLOB。
 */
struct StoredVector
{
    qint64 chunkId = 0;
    qint64 fileId = 0;
    QVector<float> values;
};

/**
 * 向量后端**可替换接口**（先小后扩：今天是 SQLite BLOB + 内存余弦，
 * 将来换专用向量库只需换一个实现，检索层不改）。
 *
 * 分层：实现位于 storage 层（`SqliteVectorIndex`），core 只依赖本接口，
 * 从而 M5 的检索逻辑可以拿假实现离线单测。
 */
class VectorIndex
{
public:
    virtual ~VectorIndex() = default;

    /// 后端是否可用（不可用时全部读写返回失败，**不崩**）
    virtual bool isAvailable() const = 0;

    /// 写入 / 覆盖一批向量；同一 `chunk_id` 重复写入为覆盖语义。
    /// 维度不一致（空向量或批内维度不同）返回 false + error，绝不写入半批
    virtual bool upsert(const QString &providerId, const QString &model,
                        const QList<StoredVector> &vectors, QString *error) = 0;

    /// 清理某文件的全部向量（换 embedding 模型重建时必须先清，避免新旧维度混用）
    virtual bool removeByFile(qint64 fileId, QString *error) = 0;

    /// 指定「厂商 + 模型」下的全部向量（按 chunk_id 升序）；M5 检索用
    virtual QList<StoredVector> vectors(const QString &providerId, const QString &model,
                                        QString *error) const = 0;

    /// 已建向量且属于该「厂商 + 模型」的片段数（0 = 尚未建立 / 延迟索引中）
    virtual int count(const QString &providerId, const QString &model, QString *error) const = 0;
};
