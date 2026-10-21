#pragma once

#include "core/library/CostEstimator.h"
#include "core/library/LibraryIndexer.h"
#include "core/library/LibraryManager.h"
#include "providers/ChatTypes.h"
#include "storage/LibraryConfigStore.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AiConfigStore;
class AiProvider;
class AppPaths;
class CollectionManager;
class DocumentStore;
class EmbeddingService;
class ImageCaptionService;
class LibraryConfigStore;
class LibraryStore;
class ProviderCatalog;
class RagRetriever;
class SecureStore;
class SqliteVectorIndex;

/**
 * 资料库 QML 门面（模块 M7）：把 storage / core 的能力封装成 UI 可直接调用的
 * 属性与命令。**本类不含检索算法**（M5）、不含解析（M3）、不碰 SQL（M1）。
 *
 * 安全约定：
 *  - 所有会外发内容的动作（启用、挂载、导入后索引、图片 caption）都以
 *    `consentGiven` 为前提；未同意时只做本地入库，不发一个请求；
 *  - API Key 仅经 SecureStore 读写，**从不进入属性、日志或错误信息**；
 *  - 删除小库 / 文件记录只删索引与库内副本，用户原文件永不删（由 LibraryManager 保证）。
 */
class LibraryController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString indexingStatus READ indexingStatus NOTIFY progressChanged)
    Q_PROPERTY(bool indexing READ indexing NOTIFY progressChanged)

    Q_PROPERTY(QVariantList collections READ collections NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList mountOptions READ mountOptions NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QVariantList folderLinks READ folderLinks NOTIFY filesChanged)
    Q_PROPERTY(qint64 currentCollectionId READ currentCollectionId NOTIFY currentChanged)
    Q_PROPERTY(QString currentCollectionName READ currentCollectionName NOTIFY currentChanged)

    Q_PROPERTY(bool consentGiven READ consentGiven NOTIFY consentChanged)
    Q_PROPERTY(bool libraryEnabled READ libraryEnabled NOTIFY consentChanged)
    Q_PROPERTY(int mountedCount READ mountedCount NOTIFY mountChanged)

    Q_PROPERTY(int topK READ topK WRITE setTopK NOTIFY retrievalChanged)
    Q_PROPERTY(int perFileLimit READ perFileLimit WRITE setPerFileLimit NOTIFY retrievalChanged)
    Q_PROPERTY(int tokenBudget READ tokenBudget WRITE setTokenBudget NOTIFY retrievalChanged)
    Q_PROPERTY(double similarityThreshold READ similarityThreshold WRITE setSimilarityThreshold
                   NOTIFY retrievalChanged)
    Q_PROPERTY(int neighborRadius READ neighborRadius WRITE setNeighborRadius NOTIFY retrievalChanged)
    Q_PROPERTY(bool ftsFallback READ ftsFallback WRITE setFtsFallback NOTIFY retrievalChanged)
    Q_PROPERTY(int chunkSize READ chunkSize WRITE setChunkSize NOTIFY retrievalChanged)
    Q_PROPERTY(int chunkOverlap READ chunkOverlap WRITE setChunkOverlap NOTIFY retrievalChanged)
    // 参考资料模式（v0.2.6）
    Q_PROPERTY(int perFileBudget READ perFileBudget WRITE setPerFileBudget NOTIFY retrievalChanged)
    Q_PROPERTY(int globalBudget READ globalBudget WRITE setGlobalBudget NOTIFY retrievalChanged)
    Q_PROPERTY(int contextReserve READ contextReserve WRITE setContextReserve NOTIFY retrievalChanged)
    Q_PROPERTY(double fileAggregateWeight READ fileAggregateWeight WRITE setFileAggregateWeight
                   NOTIFY retrievalChanged)
    Q_PROPERTY(int fallbackContextWindow READ fallbackContextWindow WRITE setFallbackContextWindow
                   NOTIFY retrievalChanged)

    Q_PROPERTY(QString embeddingProviderId READ embeddingProviderId NOTIFY embeddingChanged)
    Q_PROPERTY(QString embeddingModelId READ embeddingModelId NOTIFY embeddingChanged)
    Q_PROPERTY(bool embeddingReady READ embeddingReady NOTIFY embeddingChanged)
    Q_PROPERTY(QString embeddingStatus READ embeddingStatus NOTIFY embeddingChanged)
    Q_PROPERTY(QString captionProviderId READ captionProviderId NOTIFY captionChanged)
    Q_PROPERTY(QString captionModelId READ captionModelId NOTIFY captionChanged)
    Q_PROPERTY(bool captionReady READ captionReady NOTIFY captionChanged)
    Q_PROPERTY(QString captionStatus READ captionStatus NOTIFY captionChanged)

    Q_PROPERTY(QVariantMap estimate READ estimate NOTIFY estimateChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)

public:
    LibraryController(AppPaths paths, LibraryStore *store, LibraryConfigStore *configStore,
                       EmbeddingService *embedding, SqliteVectorIndex *vectorIndex,
                       RagRetriever *retriever, ProviderCatalog *catalog, SecureStore *secure,
                       AiConfigStore *aiConfig, DocumentStore *documentStore = nullptr,
                       QObject *parent = nullptr);
    ~LibraryController() override;

    bool available() const;
    QString message() const;
    QString indexingStatus() const;
    bool indexing() const;

    QVariantList collections() const;
    QVariantList mountOptions() const;
    QVariantList files() const;
    QVariantList folderLinks() const;
    qint64 currentCollectionId() const;
    QString currentCollectionName() const;

    bool consentGiven() const;
    bool libraryEnabled() const;
    int mountedCount() const;

    int topK() const;
    int perFileLimit() const;
    int tokenBudget() const;
    double similarityThreshold() const;
    int neighborRadius() const;
    bool ftsFallback() const;
    int chunkSize() const;
    int chunkOverlap() const;
    // 参考资料模式（v0.2.6）
    int perFileBudget() const;
    int globalBudget() const;
    int contextReserve() const;
    double fileAggregateWeight() const;
    int fallbackContextWindow() const;

    QString embeddingProviderId() const;
    QString embeddingModelId() const;
    bool embeddingReady() const;
    QString embeddingStatus() const;
    QString captionProviderId() const;
    QString captionModelId() const;
    bool captionReady() const;
    QString captionStatus() const;

    QVariantMap estimate() const;
    QVariantList searchResults() const;

    // ---- 小库 ----
    Q_INVOKABLE bool createCollection(const QString &name, const QString &description = QString());
    Q_INVOKABLE bool renameCollection(qint64 collectionId, const QString &name,
                                      const QString &description = QString());
    Q_INVOKABLE bool removeCollection(qint64 collectionId);
    Q_INVOKABLE void selectCollection(qint64 collectionId);

    // ---- 文件来源 ----
    /// 入参接受 `QUrl` 字符串（FileDialog.selectedFiles）或本地路径
    Q_INVOKABLE void importFiles(const QVariantList &fileUrls);
    Q_INVOKABLE void linkFolder(const QString &folderUrl);
    Q_INVOKABLE void rebuildFolder(qint64 linkId);
    Q_INVOKABLE void unlinkFolder(qint64 linkId);
    Q_INVOKABLE void removeFile(qint64 fileId);
    Q_INVOKABLE void removeFileFromCollection(qint64 fileId);

    // ---- 索引 ----
    Q_INVOKABLE void reindexFile(qint64 fileId);
    Q_INVOKABLE void reindexCollection();
    Q_INVOKABLE void reindexAll();
    Q_INVOKABLE void cancelIndexing();

    // ---- 同意 / 启用 / 挂载 ----
    Q_INVOKABLE void requestEnable();
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void grantConsent();
    Q_INVOKABLE void revokeConsent();
    Q_INVOKABLE void setMounted(qint64 collectionId, bool mounted);

    // ---- 检索配置 ----
    void setTopK(int value);
    void setPerFileLimit(int value);
    void setTokenBudget(int value);
    void setSimilarityThreshold(double value);
    void setNeighborRadius(int value);
    void setFtsFallback(bool value);
    void setChunkSize(int value);
    void setChunkOverlap(int value);
    // 参考资料模式（v0.2.6）
    void setPerFileBudget(int value);
    void setGlobalBudget(int value);
    void setContextReserve(int value);
    void setFileAggregateWeight(double value);
    void setFallbackContextWindow(int value);

    // ---- 服务配置 ----
    Q_INVOKABLE QVariantList embeddingProviderOptions() const;
    Q_INVOKABLE QVariantList captionProviderOptions() const;
    Q_INVOKABLE QString suggestedEmbeddingModel(const QString &providerId) const;
    Q_INVOKABLE QString suggestedCaptionModel(const QString &providerId) const;
    Q_INVOKABLE bool saveEmbeddingConfig(const QString &providerId, const QString &modelId,
                                         int dimensions = 0);
    Q_INVOKABLE bool saveEmbeddingKey(const QString &apiKey);
    Q_INVOKABLE bool saveCaptionConfig(const QString &providerId, const QString &modelId);
    Q_INVOKABLE bool saveCaptionKey(const QString &apiKey);
    Q_INVOKABLE bool hasEmbeddingKey() const;
    Q_INVOKABLE bool hasCaptionKey() const;

    // ---- 库内搜索（纯本地关键词，不联网）----
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void refresh();

signals:
    void stateChanged();
    void collectionsChanged();
    void filesChanged();
    void currentChanged();
    void consentChanged();
    void mountChanged();
    void retrievalChanged();
    void embeddingChanged();
    void captionChanged();
    void estimateChanged();
    void searchChanged();
    void progressChanged();
    /// 需要 UI 弹出「首次启用 / 图片外发」同意说明
    void consentRequired();

private:
    void loadState();
    void persist();
    void applyRetrievalConfig();
    void applyEmbeddingConfig();
    void rebuildCaptionService();
    ProviderConfig buildProviderConfig(const QString &providerId, const QString &keyPrefix) const;
    bool hasKey(const QString &keyId) const;
    void indexFiles(const QList<qint64> &fileIds);
    qint64 ensureCollection();
    void setMessage(const QString &message);
    void refreshEstimate();
    void refreshCaches();
    static QString statusLabel(const QString &status);
    static QString formatMoney(double amount, const QString &currency);

    LibraryStore *m_store = nullptr;
    LibraryConfigStore *m_configStore = nullptr;
    EmbeddingService *m_embedding = nullptr;
    SqliteVectorIndex *m_vectorIndex = nullptr;
    RagRetriever *m_retriever = nullptr;
    ProviderCatalog *m_catalog = nullptr;
    SecureStore *m_secure = nullptr;
    AiConfigStore *m_aiConfig = nullptr;
    DocumentStore *m_documentStore = nullptr;

    std::unique_ptr<LibraryManager> m_files;
    std::unique_ptr<CollectionManager> m_collections;
    /// 顺序有意义：caption 服务必须先于 provider 销毁
    std::unique_ptr<AiProvider> m_captionProvider;
    std::unique_ptr<ImageCaptionService> m_caption;
    std::unique_ptr<LibraryIndexer> m_indexer;
    CostEstimator m_cost;

    LibraryConfigData m_config;
    qint64 m_currentCollectionId = 0;
    QString m_message;
    QString m_indexingStatus;
    QVariantList m_collectionsCache;
    QVariantList m_filesCache;
    QVariantList m_folderLinksCache;
    QVariantList m_searchResults;
    QVariantMap m_estimate;
};
