#include "core/library/LibraryController.h"

#include "core/ai/ProviderCatalog.h"
#include "core/library/CollectionManager.h"
#include "core/library/ImageCaptionService.h"
#include "core/retriever/EmbeddingService.h"
#include "core/retriever/RagRetriever.h"
#include "providers/ProviderFactory.h"
#include "storage/AiConfigStore.h"
#include "storage/AppPaths.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryStore.h"
#include "storage/SecureStore.h"
#include "storage/SqliteVectorIndex.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace {

QString localPathOf(const QVariant &value)
{
    const QString raw = value.toString();
    const QUrl url(raw);
    if (url.isLocalFile())
        return url.toLocalFile();
    return raw;
}

QString formatTimestamp(qint64 milliseconds)
{
    if (milliseconds <= 0)
        return QString();
    return QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace

LibraryController::LibraryController(AppPaths paths, LibraryStore *store,
                                     LibraryConfigStore *configStore, EmbeddingService *embedding,
                                     SqliteVectorIndex *vectorIndex, RagRetriever *retriever,
                                     ProviderCatalog *catalog, SecureStore *secure,
                                     AiConfigStore *aiConfig, DocumentStore *documentStore,
                                     QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_configStore(configStore)
    , m_embedding(embedding)
    , m_vectorIndex(vectorIndex)
    , m_retriever(retriever)
    , m_catalog(catalog)
    , m_secure(secure)
    , m_aiConfig(aiConfig)
    , m_documentStore(documentStore)
    , m_files(std::make_unique<LibraryManager>(paths, store))
    , m_collections(std::make_unique<CollectionManager>(store, m_files.get()))
    , m_indexer(std::make_unique<LibraryIndexer>(store, vectorIndex, embedding))
{
    // v0.2.6: 把 DocumentStore 传给 Indexer，索引完成后自动填充 documents/sections 元数据
    if (m_indexer && documentStore) {
        m_indexer->setDocumentStore(documentStore);
    }
    m_cost.load();

    connect(m_indexer.get(), &LibraryIndexer::fileStarted, this, [this](qint64 fileId) {
        Q_UNUSED(fileId)
        m_indexingStatus = QStringLiteral("正在索引…");
        emit progressChanged();
    });
    connect(m_indexer.get(), &LibraryIndexer::fileFinished, this,
            [this](qint64 fileId, const QString &status, const QString &message) {
                Q_UNUSED(fileId)
                Q_UNUSED(status)
                Q_UNUSED(message)
                refreshCaches();
                refreshEstimate();
                emit filesChanged();
                emit collectionsChanged();
            });
    connect(m_indexer.get(), &LibraryIndexer::queueDrained, this,
            [this](int indexed, int needsEmbedding, int failed) {
                m_indexingStatus =
                    QStringLiteral("索引完成：已索引 %1 · 待建向量/待索引 %2 · 失败 %3")
                        .arg(indexed)
                        .arg(needsEmbedding)
                        .arg(failed);
                emit progressChanged();
                refresh();
            });

    loadState();
    refresh();
}

LibraryController::~LibraryController() = default;

void LibraryController::loadState()
{
    QString error;
    m_config = m_configStore ? m_configStore->load(&error) : LibraryConfigData();
    m_indexer->setConfig(m_config);
    if (m_embedding) {
        connect(m_embedding, &EmbeddingService::finished, this,
                [this](const EmbeddingResult &) { emit embeddingChanged(); });
        connect(m_embedding, &EmbeddingService::failed, this,
                [this](const ChatError &) { emit embeddingChanged(); });
    }
    applyEmbeddingConfig();
    rebuildCaptionService();
    applyRetrievalConfig();
    if (!error.isEmpty())
        setMessage(error);
}

bool LibraryController::available() const
{
    return m_store && m_store->isAvailable() && m_files && m_files->isAvailable();
}

QString LibraryController::message() const
{
    return m_message;
}

QString LibraryController::indexingStatus() const
{
    return m_indexingStatus;
}

bool LibraryController::indexing() const
{
    return m_indexer && m_indexer->isBusy();
}

QVariantList LibraryController::collections() const
{
    return m_collectionsCache;
}

QVariantList LibraryController::mountOptions() const
{
    return m_collectionsCache;
}

QVariantList LibraryController::files() const
{
    return m_filesCache;
}

QVariantList LibraryController::folderLinks() const
{
    return m_folderLinksCache;
}

qint64 LibraryController::currentCollectionId() const
{
    return m_currentCollectionId;
}

QString LibraryController::currentCollectionName() const
{
    for (const QVariant &entry : m_collectionsCache) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toLongLong() == m_currentCollectionId)
            return map.value(QStringLiteral("name")).toString();
    }
    return QString();
}

bool LibraryController::consentGiven() const
{
    return m_config.consentGiven;
}

bool LibraryController::libraryEnabled() const
{
    return m_config.enabled;
}

int LibraryController::mountedCount() const
{
    return m_config.mountedCollections.size();
}

int LibraryController::topK() const
{
    return m_config.topK;
}

int LibraryController::perFileLimit() const
{
    return m_config.perFileLimit;
}

int LibraryController::tokenBudget() const
{
    return m_config.tokenBudget;
}

double LibraryController::similarityThreshold() const
{
    return m_config.similarityThreshold;
}

int LibraryController::neighborRadius() const
{
    return m_config.neighborRadius;
}

bool LibraryController::ftsFallback() const
{
    return m_config.ftsFallback;
}

int LibraryController::chunkSize() const
{
    return m_config.chunkSize;
}

int LibraryController::chunkOverlap() const
{
    return m_config.chunkOverlap;
}

QString LibraryController::embeddingProviderId() const
{
    return m_config.embedding.providerId;
}

QString LibraryController::embeddingModelId() const
{
    return m_config.embedding.modelId;
}

bool LibraryController::embeddingReady() const
{
    return m_embedding && m_embedding->isReady() && m_config.consentGiven;
}

QString LibraryController::embeddingStatus() const
{
    if (m_config.embedding.providerId.isEmpty() || m_config.embedding.modelId.isEmpty())
        return QStringLiteral("未配置：文件仍会入本地库，标记为「待建向量」");
    if (!hasEmbeddingKey())
        return QStringLiteral("缺少 API Key（可直接复用对话厂商的 Key，或在下方单独填写）");
    if (m_catalog) {
        const CatalogProvider provider = m_catalog->provider(m_config.embedding.providerId);
        if (provider.customizable && (!m_aiConfig || m_aiConfig->load().customBaseUrl.trimmed().isEmpty()))
            return QStringLiteral("自定义厂商缺少 API 地址（在「AI 接入」中填写）");
    }
    if (!m_config.consentGiven)
        return QStringLiteral("配置就绪，需先同意内容外发才会建立向量");
    return m_embedding && m_embedding->isReady() ? QStringLiteral("就绪")
                                                 : QStringLiteral("配置不完整");
}

QString LibraryController::captionProviderId() const
{
    return m_config.caption.providerId;
}

QString LibraryController::captionModelId() const
{
    return m_config.caption.modelId;
}

bool LibraryController::captionReady() const
{
    return m_caption && m_caption->isReady() && m_config.consentGiven;
}

QString LibraryController::captionStatus() const
{
    if (m_config.caption.providerId.isEmpty() || m_config.caption.modelId.isEmpty())
        return QStringLiteral("未配置：图片会保持「待索引」，不影响其它格式");
    if (!hasCaptionKey())
        return QStringLiteral("缺少 API Key（可直接复用对话厂商的 Key，或单独填写）");
    if (!m_config.consentGiven)
        return QStringLiteral("配置就绪，需先同意图片外发");
    return m_caption && m_caption->isReady() ? QStringLiteral("就绪")
                                             : QStringLiteral("配置不完整");
}

QVariantMap LibraryController::estimate() const
{
    return m_estimate;
}

QVariantList LibraryController::searchResults() const
{
    return m_searchResults;
}

bool LibraryController::createCollection(const QString &name, const QString &description)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setMessage(QStringLiteral("小库名称不能为空"));
        return false;
    }
    QString error;
    qint64 id = 0;
    if (!m_collections->createCollection(trimmed, description.trimmed(), &id, &error)) {
        setMessage(error);
        return false;
    }
    m_currentCollectionId = id;
    refresh();
    emit currentChanged();
    setMessage(QStringLiteral("已创建小库「%1」").arg(trimmed));
    return true;
}

bool LibraryController::renameCollection(qint64 collectionId, const QString &name,
                                         const QString &description)
{
    const QString trimmed = name.trimmed();
    if (collectionId <= 0 || trimmed.isEmpty()) {
        setMessage(QStringLiteral("请选择小库并填写名称"));
        return false;
    }
    QString error;
    if (!m_collections->renameCollection(collectionId, trimmed, description.trimmed(), &error)) {
        setMessage(error);
        return false;
    }
    refresh();
    setMessage(QStringLiteral("已更新小库信息"));
    return true;
}

bool LibraryController::removeCollection(qint64 collectionId)
{
    QString error;
    if (!m_collections->removeCollection(collectionId, &error)) {
        setMessage(error);
        return false;
    }
    m_config.mountedCollections.removeAll(collectionId);
    persist();
    if (m_currentCollectionId == collectionId)
        m_currentCollectionId = 0;
    refresh();
    emit currentChanged();
    emit mountChanged();
    setMessage(QStringLiteral("已删除小库（用户原文件未做任何删除）"));
    return true;
}

void LibraryController::selectCollection(qint64 collectionId)
{
    if (m_currentCollectionId == collectionId)
        return;
    m_currentCollectionId = collectionId;
    refresh();
    emit currentChanged();
}

void LibraryController::importFiles(const QVariantList &fileUrls)
{
    QStringList paths;
    for (const QVariant &value : fileUrls) {
        const QString path = localPathOf(value);
        if (!path.isEmpty() && QFileInfo::exists(path) && !paths.contains(path))
            paths.append(path);
    }
    if (paths.isEmpty()) {
        setMessage(QStringLiteral("没有可导入的文件（未获得有效的本地文件路径）"));
        return;
    }
    if (!m_config.consentGiven) {
        // 导入只是复制进本地库，不涉及外发；真正索引时才会因未同意而停在「待建向量」
    }

    const qint64 collectionId = ensureCollection();
    if (collectionId <= 0)
        return;

    QList<qint64> ids;
    QString error;
    const bool ok = m_files->importFiles(paths, &ids, &error);
    for (qint64 fileId : ids)
        m_collections->addFileToCollection(fileId, collectionId, nullptr);
    indexFiles(ids);
    refresh();
    const QString collectionName = currentCollectionName();
    if (!ok && !error.isEmpty())
        setMessage(QStringLiteral("已导入 %1 个文件到「%2」；%3")
                       .arg(ids.size())
                       .arg(collectionName, error));
    else
        setMessage(QStringLiteral("已导入 %1 个文件到「%2」（原文件未移动、未删除）")
                       .arg(ids.size())
                       .arg(collectionName));
}

void LibraryController::linkFolder(const QString &folderUrl)
{
    const QString path = localPathOf(folderUrl);
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        setMessage(QStringLiteral("请选择一个有效文件夹"));
        return;
    }
    const qint64 collectionId = ensureCollection();
    if (collectionId <= 0)
        return;

    qint64 linkId = 0;
    FolderSnapshotResult result;
    QString error;
    if (!m_files->linkFolder(collectionId, path, &linkId, &result, &error)) {
        setMessage(error);
        return;
    }

    QList<qint64> pending;
    const QList<LibraryFileRecord> records = m_store->filesByFolderLink(linkId, &error);
    for (const LibraryFileRecord &record : records) {
        if (record.status != QStringLiteral("indexed"))
            pending.append(record.id);
    }
    indexFiles(pending);
    refresh();
    setMessage(QStringLiteral("已关联文件夹：新增 %1 · 未变化 %2 · 变化 %3 · 缺失 %4")
                   .arg(result.added)
                   .arg(result.unchanged)
                   .arg(result.markedStale)
                   .arg(result.missing));
}

void LibraryController::rebuildFolder(qint64 linkId)
{
    FolderSnapshotResult result;
    QString error;
    if (!m_files->rebuildFolder(linkId, &result, &error)) {
        setMessage(error);
        return;
    }
    QList<qint64> pending;
    const QList<LibraryFileRecord> records = m_store->filesByFolderLink(linkId, &error);
    for (const LibraryFileRecord &record : records) {
        if (record.status != QStringLiteral("indexed"))
            pending.append(record.id);
    }
    indexFiles(pending);
    refresh();
    setMessage(QStringLiteral("快照已重建：新增 %1 · 变化 %2 · 缺失 %3")
                   .arg(result.added)
                   .arg(result.markedStale)
                   .arg(result.missing));
}

void LibraryController::unlinkFolder(qint64 linkId)
{
    QString error;
    if (!m_files->unlinkFolder(linkId, &error)) {
        setMessage(error);
        return;
    }
    refresh();
    setMessage(QStringLiteral("已解除关联（用户磁盘文件未删除）"));
}

void LibraryController::removeFile(qint64 fileId)
{
    QString error;
    if (!m_files->removeFileRecord(fileId, &error)) {
        setMessage(error);
        return;
    }
    refresh();
    setMessage(QStringLiteral("已移除文件记录（关联文件夹中的源文件未删除）"));
}

void LibraryController::removeFileFromCollection(qint64 fileId)
{
    if (m_currentCollectionId <= 0)
        return;
    QString error;
    if (!m_collections->removeFileFromCollection(fileId, m_currentCollectionId, &error)) {
        setMessage(error);
        return;
    }
    refresh();
    setMessage(QStringLiteral("已从本库移除（文件记录与源文件均保留）"));
}

void LibraryController::reindexFile(qint64 fileId)
{
    m_indexer->setConfig(m_config);
    indexFiles({fileId});
}

void LibraryController::reindexCollection()
{
    if (m_currentCollectionId <= 0) {
        setMessage(QStringLiteral("请先选择一个小库"));
        return;
    }
    QString error;
    const QList<LibraryFileRecord> records =
        m_files->filesInCollection(m_currentCollectionId, &error);
    QList<qint64> ids;
    for (const LibraryFileRecord &record : records)
        ids.append(record.id);
    m_indexer->setConfig(m_config);
    indexFiles(ids);
    setMessage(QStringLiteral("已提交 %1 个文件重建索引").arg(ids.size()));
}

void LibraryController::reindexAll()
{
    QList<qint64> ids;
    for (const QVariant &entry : m_filesCache) {
        ids.append(entry.toMap().value(QStringLiteral("id")).toLongLong());
    }
    if (ids.isEmpty() && m_store) {
        QString error;
        const QList<LibraryFileRecord> all = m_store->allFiles(&error);
        for (const LibraryFileRecord &record : all)
            ids.append(record.id);
    }
    m_indexer->setConfig(m_config);
    indexFiles(ids);
    setMessage(QStringLiteral("已提交 %1 个文件重建索引").arg(ids.size()));
}

void LibraryController::cancelIndexing()
{
    m_indexer->cancel();
    m_indexingStatus = QStringLiteral("已取消索引（已入库的片段保留）");
    emit progressChanged();
}

void LibraryController::requestEnable()
{
    if (!m_config.consentGiven) {
        emit consentRequired();
        return;
    }
    setEnabled(true);
}

void LibraryController::setEnabled(bool enabled)
{
    if (enabled && !m_config.consentGiven) {
        emit consentRequired();
        return;
    }
    if (m_config.enabled == enabled)
        return;
    m_config.enabled = enabled;
    persist();
    emit consentChanged();
    setMessage(enabled ? QStringLiteral("资料库已启用")
                       : QStringLiteral("资料库已停用（对话不再检索资料库）"));
}

void LibraryController::grantConsent()
{
    m_config.consentGiven = true;
    m_config.consentAt = QDateTime::currentMSecsSinceEpoch();
    m_config.enabled = true;
    persist();
    m_indexer->setConfig(m_config);
    if (m_caption)
        m_caption->setConsentGiven(true);
    refreshEstimate();
    emit consentChanged();
    emit embeddingChanged();
    emit captionChanged();
    setMessage(QStringLiteral("已记录同意（仅存本机）：之后索引才会外发内容"));
}

void LibraryController::revokeConsent()
{
    m_config.consentGiven = false;
    m_config.enabled = false;
    m_config.mountedCollections.clear();
    persist();
    m_indexer->setConfig(m_config);
    if (m_caption)
        m_caption->setConsentGiven(false);
    refreshEstimate();
    emit consentChanged();
    emit mountChanged();
    setMessage(QStringLiteral("已撤回同意：不会再外发任何资料库内容"));
}

void LibraryController::setMounted(qint64 collectionId, bool mounted)
{
    if (collectionId <= 0)
        return;
    if (mounted && !m_config.consentGiven) {
        emit consentRequired();
        return;
    }
    if (mounted) {
        if (!m_config.mountedCollections.contains(collectionId))
            m_config.mountedCollections.append(collectionId);
        m_config.enabled = true;
    } else {
        m_config.mountedCollections.removeAll(collectionId);
    }
    persist();
    emit mountChanged();
    emit collectionsChanged();
    emit consentChanged();
    refreshEstimate();
}

void LibraryController::setTopK(int value)
{
    value = std::clamp(value, 1, 30);
    if (value == m_config.topK)
        return;
    m_config.topK = value;
    persist();
    applyRetrievalConfig();
    refreshEstimate();
    emit retrievalChanged();
}

void LibraryController::setPerFileLimit(int value)
{
    value = std::clamp(value, 1, 10);
    if (value == m_config.perFileLimit)
        return;
    m_config.perFileLimit = value;
    persist();
    applyRetrievalConfig();
    emit retrievalChanged();
}

void LibraryController::setTokenBudget(int value)
{
    value = std::clamp(value, 200, 20000);
    if (value == m_config.tokenBudget)
        return;
    m_config.tokenBudget = value;
    persist();
    applyRetrievalConfig();
    refreshEstimate();
    emit retrievalChanged();
}

void LibraryController::setSimilarityThreshold(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(value + 1.0, m_config.similarityThreshold + 1.0))
        return;
    m_config.similarityThreshold = value;
    persist();
    applyRetrievalConfig();
    emit retrievalChanged();
}

void LibraryController::setNeighborRadius(int value)
{
    value = std::clamp(value, 0, 3);
    if (value == m_config.neighborRadius)
        return;
    m_config.neighborRadius = value;
    persist();
    applyRetrievalConfig();
    emit retrievalChanged();
}

void LibraryController::setFtsFallback(bool value)
{
    if (value == m_config.ftsFallback)
        return;
    m_config.ftsFallback = value;
    persist();
    applyRetrievalConfig();
    emit retrievalChanged();
}

void LibraryController::setChunkSize(int value)
{
    value = std::clamp(value, 200, 4000);
    if (value == m_config.chunkSize)
        return;
    m_config.chunkSize = value;
    if (m_config.chunkOverlap >= value / 2)
        m_config.chunkOverlap = value / 4;
    persist();
    m_indexer->setConfig(m_config);
    emit retrievalChanged();
}

void LibraryController::setChunkOverlap(int value)
{
    value = std::clamp(value, 0, std::max(0, m_config.chunkSize / 2));
    if (value == m_config.chunkOverlap)
        return;
    m_config.chunkOverlap = value;
    persist();
    m_indexer->setConfig(m_config);
    emit retrievalChanged();
}

// ---- 参考资料模式（v0.2.6）----

int LibraryController::perFileBudget() const
{
    return m_config.perFileBudget;
}

int LibraryController::globalBudget() const
{
    return m_config.globalBudget;
}

int LibraryController::contextReserve() const
{
    return m_config.contextReserve;
}

double LibraryController::fileAggregateWeight() const
{
    return m_config.fileAggregateWeight;
}

void LibraryController::setPerFileBudget(int value)
{
    value = std::clamp(value, 1000, 200000);
    if (value == m_config.perFileBudget)
        return;
    m_config.perFileBudget = value;
    persist();
    emit retrievalChanged();
}

void LibraryController::setGlobalBudget(int value)
{
    value = std::clamp(value, 1000, 500000);
    if (value == m_config.globalBudget)
        return;
    m_config.globalBudget = value;
    persist();
    emit retrievalChanged();
}

void LibraryController::setContextReserve(int value)
{
    value = std::clamp(value, 200, 32000);
    if (value == m_config.contextReserve)
        return;
    m_config.contextReserve = value;
    persist();
    emit retrievalChanged();
}

void LibraryController::setFileAggregateWeight(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(value + 1.0, m_config.fileAggregateWeight + 1.0))
        return;
    m_config.fileAggregateWeight = value;
    persist();
    emit retrievalChanged();
}

int LibraryController::fallbackContextWindow() const
{
    return m_config.fallbackContextWindow;
}

void LibraryController::setFallbackContextWindow(int value)
{
    value = std::clamp(value, 2000, 4000000);
    if (value == m_config.fallbackContextWindow)
        return;
    m_config.fallbackContextWindow = value;
    persist();
    emit retrievalChanged();
}

QVariantList LibraryController::embeddingProviderOptions() const
{
    QVariantList options;
    if (!m_catalog)
        return options;
    for (const CatalogProvider &provider : m_catalog->providers()) {
        if (!provider.embedding.available && !provider.customizable)
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("id"), provider.id);
        map.insert(QStringLiteral("displayName"), provider.displayName);
        map.insert(QStringLiteral("available"), provider.embedding.available);
        options.append(map);
    }
    return options;
}

QVariantList LibraryController::captionProviderOptions() const
{
    QVariantList options;
    if (!m_catalog)
        return options;
    for (const CatalogProvider &provider : m_catalog->providers()) {
        bool vision = !provider.captionModel.isEmpty();
        for (const CatalogModel &model : provider.models) {
            if (model.supportsVision)
                vision = true;
        }
        if (!vision && !provider.customizable)
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("id"), provider.id);
        map.insert(QStringLiteral("displayName"), provider.displayName);
        options.append(map);
    }
    return options;
}

QString LibraryController::suggestedEmbeddingModel(const QString &providerId) const
{
    if (!m_catalog)
        return QString();
    return m_catalog->provider(providerId).embedding.defaultModelId();
}

QString LibraryController::suggestedCaptionModel(const QString &providerId) const
{
    if (!m_catalog)
        return QString();
    return m_catalog->provider(providerId).captionModel;
}

bool LibraryController::saveEmbeddingConfig(const QString &providerId, const QString &modelId,
                                            int dimensions)
{
    const QString trimmedProvider = providerId.trimmed();
    QString trimmedModel = modelId.trimmed();
    if (trimmedModel.isEmpty())
        trimmedModel = suggestedEmbeddingModel(trimmedProvider);

    m_config.embedding.providerId = trimmedProvider;
    m_config.embedding.modelId = trimmedModel;
    if (dimensions <= 0 && m_catalog)
        dimensions = m_catalog->provider(trimmedProvider).embedding.model(trimmedModel).dimensions;
    m_config.embedding.dimensions = std::max(0, dimensions);

    persist();
    applyEmbeddingConfig();
    refreshEstimate();
    emit embeddingChanged();
    setMessage(trimmedModel.isEmpty()
                   ? QStringLiteral("已保存 embedding 厂商，请补全模型名")
                   : QStringLiteral("embedding 配置已保存：%1 / %2（旧向量需重建）")
                         .arg(trimmedProvider, trimmedModel));
    return true;
}

bool LibraryController::saveEmbeddingKey(const QString &apiKey)
{
    const QString trimmed = apiKey.trimmed();
    if (trimmed.isEmpty() || !m_secure || !m_secure->isAvailable()) {
        setMessage(QStringLiteral("无法保存密钥：安全存储不可用"));
        return false;
    }
    QString error;
    const QString keyId = QStringLiteral("library.embedding.%1.api_key")
                              .arg(m_config.embedding.providerId);
    if (!m_secure->save(keyId, trimmed, &error)) {
        setMessage(QStringLiteral("密钥保存失败：%1").arg(error));
        return false;
    }
    applyEmbeddingConfig();
    emit embeddingChanged();
    setMessage(QStringLiteral("embedding 密钥已保存（仅存本机加密保险库）"));
    return true;
}

bool LibraryController::saveCaptionConfig(const QString &providerId, const QString &modelId)
{
    const QString trimmedProvider = providerId.trimmed();
    QString trimmedModel = modelId.trimmed();
    if (trimmedModel.isEmpty())
        trimmedModel = suggestedCaptionModel(trimmedProvider);

    m_config.caption.providerId = trimmedProvider;
    m_config.caption.modelId = trimmedModel;
    persist();
    rebuildCaptionService();
    refreshEstimate();
    emit captionChanged();
    setMessage(trimmedModel.isEmpty()
                   ? QStringLiteral("已保存图片描述厂商，请补全模型名")
                   : QStringLiteral("图片描述配置已保存：%1 / %2")
                         .arg(trimmedProvider, trimmedModel));
    return true;
}

bool LibraryController::saveCaptionKey(const QString &apiKey)
{
    const QString trimmed = apiKey.trimmed();
    if (trimmed.isEmpty() || !m_secure || !m_secure->isAvailable()) {
        setMessage(QStringLiteral("无法保存密钥：安全存储不可用"));
        return false;
    }
    QString error;
    const QString keyId =
        QStringLiteral("library.caption.%1.api_key").arg(m_config.caption.providerId);
    if (!m_secure->save(keyId, trimmed, &error)) {
        setMessage(QStringLiteral("密钥保存失败：%1").arg(error));
        return false;
    }
    rebuildCaptionService();
    emit captionChanged();
    setMessage(QStringLiteral("图片描述密钥已保存（仅存本机加密保险库）"));
    return true;
}

bool LibraryController::hasEmbeddingKey() const
{
    if (!m_secure)
        return false;
    if (m_config.embedding.providerId.isEmpty())
        return false;
    if (m_secure->has(QStringLiteral("library.embedding.%1.api_key")
                          .arg(m_config.embedding.providerId)))
        return true;
    return m_secure->has(SecureStore::providerApiKeyId(m_config.embedding.providerId));
}

bool LibraryController::hasCaptionKey() const
{
    if (!m_secure)
        return false;
    if (m_config.caption.providerId.isEmpty())
        return false;
    if (m_secure->has(
            QStringLiteral("library.caption.%1.api_key").arg(m_config.caption.providerId)))
        return true;
    return m_secure->has(SecureStore::providerApiKeyId(m_config.caption.providerId));
}

void LibraryController::search(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        clearSearch();
        return;
    }
    if (!m_store || !m_store->isAvailable())
        return;

    QString error;
    QList<qint64> scope;
    if (m_currentCollectionId > 0)
        scope.append(m_currentCollectionId);
    const QList<qint64> chunkIds = m_store->searchChunkIds(trimmed, scope, 30, &error);
    const QList<ChunkRecord> chunks = m_store->chunksByIds(chunkIds, &error);

    m_searchResults.clear();
    for (const ChunkRecord &chunk : chunks) {
        LibraryFileRecord file;
        if (!m_store->fileById(chunk.fileId, &file, &error))
            continue;
        QString snippet = chunk.text.simplified();
        if (snippet.size() > 160)
            snippet = snippet.left(160) + QStringLiteral("…");
        QVariantMap map;
        map.insert(QStringLiteral("fileId"), chunk.fileId);
        map.insert(QStringLiteral("fileName"), file.displayName);
        map.insert(QStringLiteral("locLabel"), chunk.locLabel);
        map.insert(QStringLiteral("snippet"), snippet);
        map.insert(QStringLiteral("ordinal"), chunk.ordinal);
        m_searchResults.append(map);
    }
    emit searchChanged();
    setMessage(m_searchResults.isEmpty()
                   ? QStringLiteral("库内未找到匹配内容")
                   : QStringLiteral("库内命中 %1 个片段").arg(m_searchResults.size()));
}

void LibraryController::clearSearch()
{
    if (m_searchResults.isEmpty())
        return;
    m_searchResults.clear();
    emit searchChanged();
}

void LibraryController::refresh()
{
    refreshCaches();
    refreshEstimate();
    emit collectionsChanged();
    emit filesChanged();
    emit estimateChanged();
}

void LibraryController::refreshCaches()
{
    m_collectionsCache.clear();
    m_filesCache.clear();
    m_folderLinksCache.clear();
    if (!m_store || !m_store->isAvailable())
        return;

    QString error;
    const QList<CollectionRecord> collections = m_collections->collections(&error);
    for (const CollectionRecord &collection : collections) {
        const QList<LibraryFileRecord> records = m_files->filesInCollection(collection.id, nullptr);
        QVariantMap map;
        map.insert(QStringLiteral("id"), collection.id);
        map.insert(QStringLiteral("name"), collection.name);
        map.insert(QStringLiteral("description"), collection.description);
        map.insert(QStringLiteral("fileCount"), records.size());
        map.insert(QStringLiteral("mounted"), m_config.mountedCollections.contains(collection.id));
        m_collectionsCache.append(map);
    }

    if (m_currentCollectionId <= 0)
        return;

    const QList<LibraryFileRecord> records = m_files->filesInCollection(m_currentCollectionId, nullptr);
    for (const LibraryFileRecord &record : records) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), record.id);
        map.insert(QStringLiteral("displayName"), record.displayName);
        map.insert(QStringLiteral("kind"), record.kind);
        map.insert(QStringLiteral("sourceKind"), record.sourceKind);
        map.insert(QStringLiteral("status"), record.status);
        map.insert(QStringLiteral("statusLabel"), statusLabel(record.status));
        map.insert(QStringLiteral("errorText"), record.errorText);
        map.insert(QStringLiteral("chunkCount"), record.chunkCount);
        map.insert(QStringLiteral("sizeBytes"), record.sizeBytes);
        map.insert(QStringLiteral("folderLinkId"), record.folderLinkId);
        map.insert(QStringLiteral("indexedAt"), formatTimestamp(record.indexedAt));
        m_filesCache.append(map);
    }

    const QList<FolderLinkRecord> links = m_files->folderLinks(m_currentCollectionId, &error);
    for (const FolderLinkRecord &link : links) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), link.id);
        map.insert(QStringLiteral("rootPath"), link.rootPath);
        map.insert(QStringLiteral("fileCount"), link.fileCount);
        map.insert(QStringLiteral("status"), link.status);
        map.insert(QStringLiteral("snapshotAt"), formatTimestamp(link.snapshotAt));
        map.insert(QStringLiteral("errorText"), link.errorText);
        m_folderLinksCache.append(map);
    }
}

void LibraryController::refreshEstimate()
{
    m_estimate.clear();
    if (!m_store || !m_store->isAvailable()) {
        emit estimateChanged();
        return;
    }

    QString error;
    QList<LibraryFileRecord> records;
    if (m_currentCollectionId > 0)
        records = m_files->filesInCollection(m_currentCollectionId, &error);
    else
        records = m_store->allFiles(&error);

    int textTokens = 0;
    int images = 0;
    for (const LibraryFileRecord &record : records) {
        if (record.kind == QStringLiteral("image")) {
            ++images;
            continue;
        }
        if (record.kind == QStringLiteral("video"))
            continue;
        const QList<ChunkRecord> chunks = m_store->chunksOfFile(record.id, nullptr);
        if (!chunks.isEmpty()) {
            for (const ChunkRecord &chunk : chunks)
                textTokens += qMax(0, chunk.tokenEst);
        } else {
            // 尚未索引：按 UTF-8 字节数粗估（中文约 3 字节/字、1 字≈1 token）
            textTokens += static_cast<int>(qMax<qint64>(0, record.sizeBytes) / 3);
        }
    }

    const CostLine embedding =
        m_cost.embeddingCost(m_config.embedding.providerId, m_config.embedding.modelId, textTokens);
    const CostLine vision =
        m_cost.visionCost(m_config.caption.providerId, m_config.caption.modelId, images);

    QString chatProvider;
    QString chatModel;
    if (m_aiConfig) {
        const AiConfigData ai = m_aiConfig->load();
        chatProvider = ai.providerId;
        chatModel = ai.modelId;
    }
    const CostLine context = m_cost.chatCost(chatProvider, chatModel, m_config.tokenBudget, 0);

    m_estimate.insert(QStringLiteral("textTokens"), textTokens);
    m_estimate.insert(QStringLiteral("imageCount"), images);
    m_estimate.insert(QStringLiteral("embeddingTokens"), embedding.tokens);
    m_estimate.insert(QStringLiteral("embeddingCost"),
                      embedding.known ? formatMoney(embedding.amount, embedding.currency)
                                      : QStringLiteral("—"));
    m_estimate.insert(QStringLiteral("embeddingKnown"), embedding.known);
    m_estimate.insert(QStringLiteral("visionImages"), vision.images);
    m_estimate.insert(QStringLiteral("visionTokens"), vision.tokens);
    m_estimate.insert(QStringLiteral("visionCost"),
                      vision.known ? formatMoney(vision.amount, vision.currency)
                                   : QStringLiteral("—"));
    m_estimate.insert(QStringLiteral("visionKnown"), vision.known);
    m_estimate.insert(QStringLiteral("contextTokens"), context.tokens);
    m_estimate.insert(QStringLiteral("contextCost"),
                      context.known ? formatMoney(context.amount, context.currency)
                                    : QStringLiteral("—"));
    m_estimate.insert(QStringLiteral("contextKnown"), context.known);
    m_estimate.insert(QStringLiteral("note"),
                      QStringLiteral("按内置价格表估算，仅作量级参考；未收录的模型只显示 token。"));
    emit estimateChanged();
}

void LibraryController::persist()
{
    if (!m_configStore)
        return;
    QString error;
    if (!m_configStore->save(m_config, &error))
        setMessage(QStringLiteral("配置保存失败：%1").arg(error));
}

void LibraryController::applyRetrievalConfig()
{
    if (!m_retriever)
        return;
    RetrievalConfig config;
    config.topK = m_config.topK;
    config.perFileLimit = m_config.perFileLimit;
    config.tokenBudget = m_config.tokenBudget;
    config.similarityThreshold = m_config.similarityThreshold;
    config.neighborRadius = m_config.neighborRadius;
    config.ftsFallback = m_config.ftsFallback;
    m_retriever->setConfig(config);
    m_retriever->setEmbeddingModel(m_config.embedding.providerId, m_config.embedding.modelId);
}

void LibraryController::applyEmbeddingConfig()
{
    if (!m_embedding)
        return;
    if (m_embedding->busy())
        m_embedding->cancel();
    m_embedding->configure(
        buildProviderConfig(m_config.embedding.providerId, QStringLiteral("library.embedding.")),
        m_config.embedding);
    if (m_retriever)
        m_retriever->setEmbeddingModel(m_config.embedding.providerId, m_config.embedding.modelId);
}

void LibraryController::rebuildCaptionService()
{
    // 先摘掉索引器持有的裸指针，再销毁服务：避免 setCaptionService 比较到悬空指针
    m_indexer->setCaptionService(nullptr);
    m_caption.reset();
    m_captionProvider.reset();
    if (!m_catalog || m_config.caption.providerId.isEmpty()
        || m_config.caption.modelId.isEmpty()) {
        m_indexer->setCaptionService(nullptr);
        emit captionChanged();
        return;
    }

    const CatalogProvider provider = m_catalog->provider(m_config.caption.providerId);
    const AiProvider::Protocol protocol = provider.protocol == QStringLiteral("anthropic")
        ? AiProvider::Anthropic
        : AiProvider::OpenAI;
    m_captionProvider = ProviderFactory::create(protocol);
    if (!m_captionProvider) {
        m_indexer->setCaptionService(nullptr);
        emit captionChanged();
        return;
    }
    ProviderConfig config =
        buildProviderConfig(m_config.caption.providerId, QStringLiteral("library.caption."));
    config.model = m_config.caption.modelId;
    m_captionProvider->configure(config);

    m_caption = std::make_unique<ImageCaptionService>(m_captionProvider.get());
    m_caption->setCaptionConfig(m_config.caption);
    m_caption->setConsentGiven(m_config.consentGiven);
    m_indexer->setCaptionService(m_caption.get());
    emit captionChanged();
}

ProviderConfig LibraryController::buildProviderConfig(const QString &providerId,
                                                      const QString &keyPrefix) const
{
    ProviderConfig config;
    config.providerId = providerId;
    if (providerId.isEmpty() || !m_catalog)
        return config;

    const CatalogProvider provider = m_catalog->provider(providerId);
    config.baseUrl = provider.baseUrl;
    config.authStyle = provider.authStyle;
    config.extraHeaders = provider.extraHeaders;
    if (provider.customizable && m_aiConfig) {
        const AiConfigData data = m_aiConfig->load();
        if (!data.customBaseUrl.trimmed().isEmpty())
            config.baseUrl = data.customBaseUrl;
    }
    if (m_secure) {
        QString key;
        if (m_secure->load(keyPrefix + providerId + QStringLiteral(".api_key"), &key, nullptr)
            && !key.isEmpty()) {
            config.apiKey = key;
        } else if (m_secure->load(SecureStore::providerApiKeyId(providerId), &key, nullptr)) {
            config.apiKey = key;
        }
    }
    return config;
}

void LibraryController::indexFiles(const QList<qint64> &fileIds)
{
    if (fileIds.isEmpty())
        return;
    m_indexer->setConfig(m_config);
    m_indexingStatus = QStringLiteral("已提交 %1 个文件").arg(fileIds.size());
    emit progressChanged();
    m_indexer->enqueueFiles(fileIds);
}

qint64 LibraryController::ensureCollection()
{
    if (m_currentCollectionId > 0)
        return m_currentCollectionId;

    QString error;
    qint64 id = 0;
    if (!m_collections->createCollection(QStringLiteral("默认资料库"), QString(), &id, &error)) {
        setMessage(error);
        return 0;
    }
    m_currentCollectionId = id;
    refresh();
    emit currentChanged();
    return id;
}

void LibraryController::setMessage(const QString &message)
{
    m_message = message;
    emit stateChanged();
}

QString LibraryController::statusLabel(const QString &status)
{
    if (status == QStringLiteral("indexed"))
        return QStringLiteral("已索引");
    if (status == QStringLiteral("needs_embedding"))
        return QStringLiteral("待建向量");
    if (status == QStringLiteral("pending"))
        return QStringLiteral("待索引");
    if (status == QStringLiteral("no_text_layer"))
        return QStringLiteral("无文本层");
    if (status == QStringLiteral("failed"))
        return QStringLiteral("索引失败");
    if (status == QStringLiteral("stale"))
        return QStringLiteral("内容已变化");
    if (status == QStringLiteral("unsupported"))
        return QStringLiteral("不支持");
    return status;
}

QString LibraryController::formatMoney(double amount, const QString &currency)
{
    const QString symbol = currency == QStringLiteral("USD") ? QStringLiteral("$")
                                                             : QStringLiteral("¥");
    const int decimals = amount > 0.0 && amount < 0.01 ? 4 : 2;
    return QStringLiteral("%1%2").arg(symbol, QString::number(amount, 'f', decimals));
}
