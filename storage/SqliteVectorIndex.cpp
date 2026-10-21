#include "storage/SqliteVectorIndex.h"

#include "storage/Database.h"

#include <QDataStream>
#include <QDateTime>
#include <QIODevice>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {

constexpr int kFloatBytes = int(sizeof(float));

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

}  // namespace

SqliteVectorIndex::SqliteVectorIndex(Database *database)
    : m_database(database)
{
}

bool SqliteVectorIndex::isAvailable() const
{
    return m_database && m_database->isOpen();
}

QByteArray SqliteVectorIndex::encode(const QVector<float> &values)
{
    QByteArray blob;
    blob.reserve(values.size() * kFloatBytes);
    QDataStream stream(&blob, QIODevice::WriteOnly);
    // 固定小端 + float32：Android 与桌面读写同一份字节，不带平台差异
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (float value : values) {
        stream << value;
    }
    return blob;
}

QVector<float> SqliteVectorIndex::decode(const QByteArray &blob)
{
    QVector<float> values;
    if (blob.isEmpty() || blob.size() % kFloatBytes != 0) {
        return values;  // 长度不是 4 的整数倍 → 视为损坏，返回空向量
    }
    values.reserve(blob.size() / kFloatBytes);
    QDataStream stream(blob);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    while (!stream.atEnd()) {
        float value = 0.0f;
        stream >> value;
        values.append(value);
    }
    return values;
}

bool SqliteVectorIndex::upsert(const QString &providerId, const QString &model,
                               const QList<StoredVector> &vectors, QString *error)
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法写入向量"));
        return false;
    }
    if (providerId.trimmed().isEmpty() || model.trimmed().isEmpty()) {
        setError(error, QStringLiteral("缺少 embedding 厂商或模型标识，无法写入向量"));
        return false;
    }
    if (vectors.isEmpty()) {
        setError(error, QStringLiteral("向量批次为空"));
        return false;
    }

    // 维度校验：空向量或批内维度不一致一律**整批拒绝**，避免半批脏数据
    const int dim = vectors.first().values.size();
    if (dim <= 0) {
        setError(error, QStringLiteral("向量为空（维度 0），无法写入"));
        return false;
    }
    for (const StoredVector &vector : vectors) {
        if (vector.chunkId <= 0) {
            setError(error, QStringLiteral("片段 id 无效，无法写入向量"));
            return false;
        }
        if (vector.values.size() != dim) {
            setError(error, QStringLiteral("批内向量维度不一致（%1 / %2），已整批拒绝")
                                .arg(dim)
                                .arg(vector.values.size()));
            return false;
        }
    }

    QSqlDatabase db = m_database->handle();
    if (!db.transaction()) {
        setError(error, QStringLiteral("无法开启向量写入事务：%1").arg(db.lastError().text()));
        return false;
    }

    const qint64 now = nowMillis();
    for (const StoredVector &vector : vectors) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO chunk_vectors (chunk_id, provider_id, model, dim, vector,"
            " created_at) VALUES (?, ?, ?, ?, ?, ?)"));
        query.addBindValue(vector.chunkId);
        query.addBindValue(providerId.trimmed());
        query.addBindValue(model.trimmed());
        query.addBindValue(dim);
        query.addBindValue(encode(vector.values));
        query.addBindValue(now);
        if (!query.exec()) {
            db.rollback();
            setError(error, QStringLiteral("写入向量失败：%1").arg(query.lastError().text()));
            return false;
        }
    }

    if (!db.commit()) {
        db.rollback();
        setError(error, QStringLiteral("提交向量写入失败：%1").arg(db.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteVectorIndex::removeByFile(qint64 fileId, QString *error)
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法清理向量"));
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM chunk_vectors WHERE chunk_id IN"
                                 " (SELECT id FROM chunks WHERE file_id = ?)"));
    query.addBindValue(fileId);
    if (!query.exec()) {
        setError(error, QStringLiteral("清理向量失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<StoredVector> SqliteVectorIndex::vectors(const QString &providerId, const QString &model,
                                               QString *error) const
{
    QList<StoredVector> result;
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法读取向量"));
        return result;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT chunk_vectors.chunk_id, chunks.file_id,"
                                 " chunk_vectors.vector FROM chunk_vectors"
                                 " JOIN chunks ON chunks.id = chunk_vectors.chunk_id"
                                 " WHERE chunk_vectors.provider_id = ? AND chunk_vectors.model = ?"
                                 " ORDER BY chunk_vectors.chunk_id ASC"));
    query.addBindValue(providerId.trimmed());
    query.addBindValue(model.trimmed());
    if (!query.exec()) {
        setError(error, QStringLiteral("读取向量失败：%1").arg(query.lastError().text()));
        return result;
    }

    while (query.next()) {
        StoredVector vector;
        vector.chunkId = query.value(0).toLongLong();
        vector.fileId = query.value(1).toLongLong();
        vector.values = decode(query.value(2).toByteArray());
        if (!vector.values.isEmpty()) {
            result.append(vector);
        }
    }
    return result;
}

int SqliteVectorIndex::count(const QString &providerId, const QString &model, QString *error) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("数据库不可用，无法统计向量"));
        return 0;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM chunk_vectors"
                                 " WHERE provider_id = ? AND model = ?"));
    query.addBindValue(providerId.trimmed());
    query.addBindValue(model.trimmed());
    if (!query.exec() || !query.next()) {
        setError(error, QStringLiteral("统计向量失败：%1").arg(query.lastError().text()));
        return 0;
    }
    return query.value(0).toInt();
}
