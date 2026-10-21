#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * 厂商 / 模型目录（数据驱动，功能 #3）。
 *
 * 数据来源：`resources/providers.json`（内嵌资源，与主题预设 `resources/themes/*.json` 同思路）。
 * 本类**只读资源、只做解析**：不碰网络、不碰存储、不依赖 QtQml，
 * 从而保证 core 不反向依赖 ui，且新增厂商＝新增 JSON 条目（零代码）。
 *
 * 硬性约定：`baseUrl` / `authStyle` / 鉴权字段 / 思考参数原生字段均以厂商官方文档为准，
 * 出处记录在 `docs/DEVLOG.md`（禁止凭记忆猜测）。
 */

/// 单个模型的思考能力声明：**UI 只渲染 levels 中列出的档位**（未声明的档位不出现、不报错）
struct CatalogThinkingSpec
{
    QStringList levels;    ///< 统一档位子集，取自 off / low / medium / high
    QJsonObject native;    ///< 档位 -> 原生参数片段；值可为 null（表示该档不注入任何字段）

    bool supportsThinking() const { return !levels.isEmpty(); }
    bool hasLevel(const QString &level) const { return levels.contains(level); }

    /// 该档位对应的原生片段；未声明或值为 null 时返回空对象（＝不注入）
    QJsonObject nativeFor(const QString &level) const;
};

struct CatalogModel
{
    QString id;
    QString displayName;
    bool supportsVision = false;
    bool isDefault = false;
    int contextWindow = 0;       ///< 上下文窗口大小（token）；0 / 负数 = 未知
    CatalogThinkingSpec thinking;

    bool isValid() const { return !id.isEmpty(); }
};

/// embedding 模型声明（v0.2.5 资料库）：同一厂商可提供多个向量模型
struct CatalogEmbeddingModel
{
    QString id;
    QString displayName;
    int dimensions = 0;  ///< 0 = 使用服务端默认维度
    bool isDefault = false;
};

/// 厂商的 embedding 能力（OpenAI 兼容 `/embeddings`）；不可用时由 UI 引导另选服务
struct CatalogEmbedding
{
    bool available = false;
    QList<CatalogEmbeddingModel> models;

    QString defaultModelId() const;
    CatalogEmbeddingModel model(const QString &modelId) const;
};

/// 厂商**内置**联网搜索能力声明（v0.2.4）。
/// 有内置能力即可「零配置」：通常直接复用该厂商的 API Key，无需用户另填搜索密钥。
struct CatalogNativeSearch
{
    bool available = false;         ///< 是否声明了可用的内置搜索
    QString protocol;               ///< 协议标识（如 deepseek），驱动 WebSearchProvider 走对应分支
    QString searchEndpoint;         ///< 检索端点
    QString fetchEndpoint;          ///< 网页抓取端点（可为空：仅搜索、不抓正文）
    bool reuseProviderKey = false;  ///< 是否复用该厂商的 API Key（true = 零配置）

    bool isValid() const { return available && !searchEndpoint.isEmpty(); }
};

struct CatalogProvider
{
    QString id;
    QString displayName;
    QString protocol;                   ///< openai | anthropic | custom
    QString baseUrl;
    QString authStyle;                  ///< bearer | x-api-key | query-key
    QJsonObject extraHeaders;           ///< 随请求固定携带的额外头部（如 anthropic-version）
    QString validationMethod;           ///< models（GET /models）| chat-minimal（最小对话请求）
    bool customizable = false;          ///< 允许用户在 UI 手填 baseUrl / model
    CatalogNativeSearch nativeSearch;   ///< 内置联网搜索能力（可选）
    CatalogEmbedding embedding;         ///< 向量化能力（资料库用）
    QString captionModel;               ///< 图片 caption 的建议视觉模型（空 = 未声明）
    QList<CatalogModel> models;

    bool isValid() const { return !id.isEmpty() && !protocol.isEmpty(); }
    CatalogModel model(const QString &modelId) const;
    QString defaultModelId() const;     ///< isDefault 优先，其次首个模型；无模型返回空串
};

class ProviderCatalog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    /// resourcePath 默认指向内嵌资源 `:/providers/providers.json`
    explicit ProviderCatalog(QString resourcePath = defaultResourcePath(), QObject *parent = nullptr);

    static QString defaultResourcePath();

    /// 解析目录；失败（文件缺失 / 非法 JSON / 无任何合法厂商）返回 false 并写 error，
    /// 单条厂商非法时**跳过该条并记警告**，不影响其余条目可用性。
    bool load(QString *error = nullptr);

    bool available() const;
    QString lastError() const;
    QStringList warnings() const;

    QList<CatalogProvider> providers() const;
    QStringList providerIds() const;

    /// 按 id 查厂商；未知 id 返回 `custom` 条目（找不到时返回无效条目），绝不返回悬空引用
    CatalogProvider provider(const QString &providerId) const;

    // ---- QML 友好视图（UI 只绑定这些，不自行解析 JSON）----
    Q_INVOKABLE QVariantList providerOptions() const;
    Q_INVOKABLE QVariantList modelOptions(const QString &providerId) const;
    Q_INVOKABLE QVariantMap modelInfo(const QString &providerId, const QString &modelId) const;

signals:
    void changed();

private:
    void setLastError(const QString &message);

    QString m_resourcePath;
    QList<CatalogProvider> m_providers;
    QString m_lastError;
    QStringList m_warnings;
    bool m_available = false;
};
