#include "core/library/LibraryIndexer.h"

#include "core/library/ImageCaptionService.h"
#include "core/library/DocumentMeta.h"
#include "core/retriever/ChunkSplitter.h"
#include "core/retriever/EmbeddingService.h"
#include "core/retriever/VectorIndex.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryStore.h"
#include "storage/SqliteVectorIndex.h"

#include <QDateTime>
#include <QHash>
#include <QMetaObject>

namespace {

/// 状态字符串与 UI / schema v2 约定保持一致
constexpr auto kStatusIndexed = "indexed";
constexpr auto kStatusNeedsEmbedding = "needs_embedding";
constexpr auto kStatusPending = "pending";
constexpr auto kStatusNoText = "no_text_layer";
constexpr auto kStatusFailed = "failed";
constexpr auto kStatusUnsupported = "unsupported";

QString captionTaskMessage(const ChatError &error)
{
    switch (error.code) {
    case ChatError::ImagesNotConfirmed:
        return QStringLiteral("未同意图片外发，待索引");
    case ChatError::NotConfigured:
        return QStringLiteral("未配置可用的视觉模型，待索引");
    case ChatError::Cancelled:
        return QStringLiteral("已取消，待索引");
    case ChatError::RateLimited:
        return QStringLiteral("视觉服务限流，待索引（稍后重建即可）");
    case ChatError::NetworkUnavailable:
        return QStringLiteral("网络不可用，待索引（恢复后重建即可）");
    case ChatError::Timeout:
        return QStringLiteral("视觉服务超时，待索引（稍后重建即可）");
    default:
        return QStringLiteral("图片描述失败：%1（待索引）")
            .arg(error.message.isEmpty() ? QStringLiteral("未知原因") : error.message);
    }
}

} // namespace

LibraryIndexer::LibraryIndexer(LibraryStore *store, SqliteVectorIndex *index,
                               EmbeddingService *embedding, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_index(index)
    , m_embedding(embedding)
{
    if (m_embedding) {
        connect(m_embedding, &EmbeddingService::finished, this,
                [this](const EmbeddingResult &result) {
                    if (m_phase != Phase::Embedding || m_currentId == 0)
                        return;
                    if (!result.ok) {
                        onEmbeddingFailed();
                        return;
                    }
                    if (result.vectors.size() != m_chunkIds.size()) {
                        finishFile(kStatusNeedsEmbedding,
                                   QStringLiteral("向量数量与片段不一致，待重建向量"));
                        return;
                    }
                    QList<StoredVector> vectors;
                    vectors.reserve(m_chunkIds.size());
                    for (int i = 0; i < m_chunkIds.size(); ++i) {
                        StoredVector vector;
                        vector.chunkId = m_chunkIds.at(i);
                        vector.fileId = m_currentId;
                        vector.values = result.vectors.at(i);
                        vectors.append(vector);
                    }
                    QString error;
                    const ProviderConfig provider = m_embedding->providerConfig();
                    const LibraryEmbeddingConfig config = m_embedding->embeddingConfig();
                    if (!m_index
                        || !m_index->upsert(provider.providerId, config.modelId, vectors, &error)) {
                        finishFile(kStatusNeedsEmbedding,
                                   QStringLiteral("向量写入失败：%1").arg(error));
                        return;
                    }
                    if (result.usage.promptTokens > 0)
                        emit usageRecorded(result.usage.promptTokens, 0);
                    finishFile(kStatusIndexed, QString());
                });
        connect(m_embedding, &EmbeddingService::failed, this, [this](const ChatError &error) {
            if (m_phase != Phase::Embedding || m_currentId == 0)
                return;
            QString message = QStringLiteral("向量化失败，待重建");
            if (!error.message.isEmpty())
                message = QStringLiteral("向量化失败：%1（待重建）").arg(error.message);
            finishFile(kStatusNeedsEmbedding, message);
        });
    }
}

void LibraryIndexer::setConfig(const LibraryConfigData &config)
{
    m_config = config;
}

void LibraryIndexer::setCaptionService(ImageCaptionService *caption)
{
    if (m_caption == caption)
        return;
    if (m_caption)
        m_caption->disconnect(this);
    m_caption = caption;
    if (!m_caption)
        return;
    m_caption->setCaptionConfig(m_config.caption);
    m_caption->setConsentGiven(m_config.consentGiven);
    connect(m_caption, &ImageCaptionService::captionReady, this,
            [this](qint64 fileId, int ordinal, const QString &caption, const ChatUsage &usage) {
                if (m_phase != Phase::Captioning || m_currentId != fileId)
                    return;
                if (usage.promptTokens > 0)
                    emit usageRecorded(0, usage.promptTokens);
                ExtractedSegment segment;
                segment.text = caption;
                segment.locKind = LocatorKind::Image;
                segment.locLabel = QStringLiteral("图片 #%1").arg(ordinal);
                saveSegments({segment});
            });
    connect(m_caption, &ImageCaptionService::captionFailed, this,
            [this](qint64 fileId, const ChatError &error) {
                if (m_phase != Phase::Captioning || m_currentId != fileId)
                    return;
                finishFile(kStatusPending, captionTaskMessage(error));
            });
}

void LibraryIndexer::setDocumentStore(DocumentStore *docStore)
{
    m_docStore = docStore;
}

bool LibraryIndexer::isAvailable() const
{
    return m_store && m_store->isAvailable();
}

bool LibraryIndexer::isBusy() const
{
    return m_currentId != 0 || !m_queue.isEmpty();
}

int LibraryIndexer::queuedCount() const
{
    return m_queue.size();
}

void LibraryIndexer::enqueueFile(qint64 fileId)
{
    if (fileId <= 0)
        return;
    m_queue.enqueue(fileId);
    if (m_currentId == 0)
        QMetaObject::invokeMethod(this, [this] { processNext(); }, Qt::QueuedConnection);
}

void LibraryIndexer::enqueueFiles(const QList<qint64> &fileIds)
{
    for (qint64 id : fileIds)
        enqueueFile(id);
}

void LibraryIndexer::cancel()
{
    m_queue.clear();
    if (m_embedding)
        m_embedding->cancel();
    if (m_caption)
        m_caption->cancelAll();
    m_currentId = 0;
    m_phase = Phase::Idle;
    m_chunkIds.clear();
    m_chunkTexts.clear();
    resetRound();
}

void LibraryIndexer::processNext()
{
    if (m_phase != Phase::Idle || m_currentId != 0)
        return;
    if (m_queue.isEmpty()) {
        if (m_roundActive) {
            const int indexed = m_indexed;
            const int needs = m_needsEmbedding;
            const int failed = m_failed;
            resetRound();
            emit queueDrained(indexed, needs, failed);
        }
        return;
    }
    m_roundActive = true;
    m_currentId = m_queue.dequeue();
    emit fileStarted(m_currentId);
    indexCurrent();
}

void LibraryIndexer::indexCurrent()
{
    if (!isAvailable()) {
        finishFile(kStatusFailed, QStringLiteral("数据库不可用"));
        return;
    }

    LibraryFileRecord record;
    QString error;
    if (!m_store->fileById(m_currentId, &record, &error)) {
        finishFile(kStatusFailed, error);
        return;
    }

    if (record.kind == QStringLiteral("video")) {
        finishFile(kStatusUnsupported, QStringLiteral("视频暂不支持索引"));
        return;
    }

    m_phase = Phase::Extracting;

    if (record.kind == QStringLiteral("image")) {
        if (!m_caption) {
            finishFile(kStatusPending, QStringLiteral("未配置图片描述服务，待索引"));
            return;
        }
        m_caption->setCaptionConfig(m_config.caption);
        m_caption->setConsentGiven(m_config.consentGiven);
        if (!m_config.consentGiven) {
            finishFile(kStatusPending, QStringLiteral("未同意图片外发，待索引"));
            return;
        }
        if (!m_caption->isReady()) {
            finishFile(kStatusPending, QStringLiteral("未配置可用的视觉模型，待索引"));
            return;
        }
        m_phase = Phase::Captioning;
        m_caption->captionItem(record.id, record.sourcePath, 1);
        return;
    }

    ExtractionResult result;
    QString extractError;
    if (!m_registry.extract(record.sourcePath, &result, &extractError)) {
        finishFile(kStatusFailed, extractError);
        return;
    }
    if (result.segments.isEmpty()) {
        finishFile(kStatusNoText, QStringLiteral("未抽取到可用文本（可能是扫描件）"));
        return;
    }
    saveSegments(result.segments);
}

void LibraryIndexer::saveSegments(const QList<ExtractedSegment> &segments)
{
    ChunkSplitOptions options;
    options.chunkSize = m_config.chunkSize;
    options.overlap = m_config.chunkOverlap;
    const ChunkSplitter splitter(options);
    m_currentHeadings.clear();
    const QList<ChunkRecord> chunks = splitter.split(m_currentId, segments, &m_currentHeadings);
    if (chunks.isEmpty()) {
        finishFile(kStatusNoText, QStringLiteral("未产生可用片段"));
        return;
    }

    QString error;
    if (!m_store->replaceChunks(m_currentId, chunks, &error)) {
        finishFile(kStatusFailed, QStringLiteral("片段写入失败：%1").arg(error));
        return;
    }

    m_chunkIds.clear();
    m_chunkTexts.clear();
    const QList<ChunkRecord> stored = m_store->chunksOfFile(m_currentId, &error);
    for (const ChunkRecord &chunk : stored) {
        m_chunkIds.append(chunk.id);
        m_chunkTexts.append(chunk.text);
    }
    beginEmbedding();
}

void LibraryIndexer::beginEmbedding()
{
    if (m_chunkIds.isEmpty()) {
        finishFile(kStatusNoText, QStringLiteral("未产生可用片段"));
        return;
    }
    if (!m_config.consentGiven) {
        finishFile(kStatusNeedsEmbedding,
                   QStringLiteral("未同意内容外发：片段已入库，同意后可重建向量"));
        return;
    }
    if (!m_embedding || !m_embedding->isReady()) {
        finishFile(kStatusNeedsEmbedding,
                   QStringLiteral("未配置 embedding：片段已入库，配置后可重建向量"));
        return;
    }
    if (m_embedding->busy()) {
        finishFile(kStatusNeedsEmbedding,
                   QStringLiteral("向量化服务正忙：片段已入库，稍后重建即可"));
        return;
    }
    m_phase = Phase::Embedding;
    m_embedding->embed(m_chunkTexts);
}

void LibraryIndexer::onEmbeddingFailed()
{
    finishFile(kStatusNeedsEmbedding, QStringLiteral("向量化失败，待重建"));
}

void LibraryIndexer::finishFile(const QString &status, const QString &message)
{
    const qint64 fileId = m_currentId;
    if (fileId <= 0) {
        m_phase = Phase::Idle;
        QMetaObject::invokeMethod(this, [this] { processNext(); }, Qt::QueuedConnection);
        return;
    }

    LibraryFileRecord record;
    QString error;
    if (isAvailable() && m_store->fileById(fileId, &record, &error)) {
        record.status = status;
        record.errorText = message;
        if (status != QString::fromLatin1(kStatusFailed))
            record.chunkCount = m_chunkIds.size();
        if (status == QString::fromLatin1(kStatusIndexed))
            record.indexedAt = QDateTime::currentMSecsSinceEpoch();
        m_store->updateFile(record, &error);
    }

    // ---------------------------------------------------------------- v0.2.6: 填充 documents/sections 元数据
    // 仅当 chunks 已经入库（非 failed / unsupported / no_text_layer）时才做
    const bool hasChunks = (status != QString::fromLatin1(kStatusFailed)
                           && status != QString::fromLatin1(kStatusUnsupported)
                           && status != QString::fromLatin1(kStatusNoText));
    if (hasChunks && m_docStore && m_docStore->isAvailable() && !m_chunkIds.isEmpty()) {
        QList<ChunkRecord> allChunks = m_store->chunksOfFile(fileId, &error);
        if (!allChunks.isEmpty()) {
            const DocumentMeta meta = detectDocumentMeta(allChunks, m_currentHeadings);

            DocumentRecord docRec;
            docRec.fileId = fileId;
            docRec.tokenEstTotal = meta.tokenEstTotal;
            docRec.sectionSource = meta.sectionSource;
            docRec.sectionCount = meta.sections.size();
            docRec.builtAt = QDateTime::currentMSecsSinceEpoch();

            qint64 docId = 0;
            if (m_docStore->upsertDocument(docRec, &docId, &error)) {
                QList<SectionRecord> sectionRecs;
                sectionRecs.reserve(meta.sections.size());
                for (int i = 0; i < meta.sections.size(); ++i) {
                    SectionRecord sr;
                    sr.ordinal = i;
                    sr.title = meta.sections[i].title;
                    sr.chunkStart = meta.sections[i].chunkStart;
                    sr.chunkEnd = meta.sections[i].chunkEnd;
                    sr.tokenEst = meta.sections[i].tokenEst;
                    sectionRecs.append(sr);
                }
                m_docStore->replaceSections(docId, sectionRecs, nullptr);
            }
        }
    }

    if (status == QString::fromLatin1(kStatusIndexed))
        ++m_indexed;
    else if (status == QString::fromLatin1(kStatusNeedsEmbedding)
             || status == QString::fromLatin1(kStatusPending))
        ++m_needsEmbedding;
    else
        ++m_failed;

    emit fileFinished(fileId, status, message);

    m_currentId = 0;
    m_phase = Phase::Idle;
    m_chunkIds.clear();
    m_chunkTexts.clear();
    QMetaObject::invokeMethod(this, [this] { processNext(); }, Qt::QueuedConnection);
}

void LibraryIndexer::resetRound()
{
    m_roundActive = false;
    m_indexed = 0;
    m_needsEmbedding = 0;
    m_failed = 0;
}
