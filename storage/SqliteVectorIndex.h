#pragma once

#include "core/retriever/VectorIndex.h"

class Database;

/**
 * 向量索引的 SQLite 实现（storage 层，模块 M4）。
 *
 * 只做一件事：把 `QVector<float>` 与 `chunk_vectors` 表之间做往返。
 *  - 存储格式：**float32 小端** 连续字节（跨平台一致，Android/桌面同格式）；
 *  - 按「厂商 + 模型」隔离，换模型不会串数据；
 *  - 维度不一致时**整批拒绝**，绝不写入半批（否则检索期会出现维度错位的脏数据）；
 *  - 失败一律 `false + error`，绝不抛异常。相似度计算**不在本层**（M5 在内存里算）。
 */
class SqliteVectorIndex : public VectorIndex
{
public:
    /// database 为空或未打开时 `isAvailable()` 为 false，全部读写安全失败
    explicit SqliteVectorIndex(Database *database);

    bool isAvailable() const override;
    bool upsert(const QString &providerId, const QString &model,
                const QList<StoredVector> &vectors, QString *error) override;
    bool removeByFile(qint64 fileId, QString *error) override;
    QList<StoredVector> vectors(const QString &providerId, const QString &model,
                                QString *error) const override;
    int count(const QString &providerId, const QString &model, QString *error) const override;

    /// float32 小端编码 / 解码（公开为静态：便于单测直接验证字节口径与往返一致）
    static QByteArray encode(const QVector<float> &values);
    static QVector<float> decode(const QByteArray &blob);

private:
    Database *m_database = nullptr;
};
