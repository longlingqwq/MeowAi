#pragma once

#include "providers/ChatTypes.h"
#include "providers/NetworkTransport.h"
#include "storage/LibraryConfigStore.h"

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QVector>

class NetworkTransport;

/**
 * 一次 embedding 请求（可能由多个批次串行组成）的结果。
 * 失败时 `ok == false`，`error` 为**结构化**原因（401 / 429 / 超时 / 断网 / 维度不符…）。
 */
struct EmbeddingResult
{
    bool ok = false;
    QList<QVector<float>> vectors;  ///< 与输入**一一对应、顺序一致**
    ChatUsage usage;                ///< 各批次用量累加；厂商未返回为 -1
    int dimensions = 0;             ///< 实际维度（0 表示无结果）
    ChatError error;
};

/**
 * embedding 服务（模块 M4）。
 *
 * 协议口径：**OpenAI 兼容的 `POST /embeddings`**（`{"model","input","dimensions"}`）。
 * 国内主流厂商（通义千问兼容模式 / 智谱 / Kimi / DeepSeek）与 OpenAI 同一套格式，
 * 因此复用同一实现、靠 `ProviderConfig` 切换 baseUrl / 鉴权，**不新增厂商分支**。
 *
 * 分层与安全边界：
 *  - 请求一律经 `NetworkTransport` 发出（可注入假实现离线单测）；
 *  - Key 只来自调用方传入的 `ProviderConfig`，本类**不读 SecureStore、不落盘、不写日志**；
 *  - 错误信息**不得**包含 Key、请求体或文件内容；
 *  - **延迟索引**：未配置 Key / 模型 / 地址时 `isReady()` 为 false，`embed()` 直接回
 *    `NotConfigured` 且**一个请求都不发**（上层据此把文件标记为 `needs_embedding`，
 *    配好服务后重建即可，不入库失败、不崩）。
 *
 * 批次：按 `LibraryEmbeddingConfig::batchSize` 串行切批（单实例同一时刻只承载一个在途请求）。
 */
class EmbeddingService : public QObject
{
    Q_OBJECT

public:
    /// 校验状态：供资料库设置页绑定
    enum ValidationStatus {
        Idle,
        Checking,
        Valid,
        Invalid,
        Unverifiable,
    };
    Q_ENUM(ValidationStatus)

    /// transport 为空时自建 `QNetworkTransport`（父对象为本对象）；注入时所有权归调用方
    explicit EmbeddingService(NetworkTransport *transport = nullptr, QObject *parent = nullptr);

    void configure(const ProviderConfig &provider, const LibraryEmbeddingConfig &embedding);
    ProviderConfig providerConfig() const;
    LibraryEmbeddingConfig embeddingConfig() const;

    /// 已具备发起 embedding 的必要配置（Key + 模型 + 地址；不含「是否可用」的联网验证）
    bool isReady() const;
    /// 本服务是否未完成（在途请求或尚有批次）
    bool busy() const;

    /// 异步批量向量化；结果经 `finished` / `failed` 回传，绝不阻塞
    void embed(const QStringList &texts);
    /// 中断在途批次并清空剩余（回 `Cancelled`）
    void cancel();

    /**
     * 显式校验当前配置：发一次单条 "ping" 的 embedding 请求。
     *
     * 只读配置、不写库、不改索引状态；校验期间重复调用直接忽略。
     * 结论经 `validationChanged` 通知，失败原因固定为中文、绝不含 Key/请求体。
     */
    void validateConfig();
    /// 配置变更后把结论复位为 idle（避免旧结论误导）
    void resetValidation();
    QString validationStatus() const;
    QString validationMessage() const;

    /**
     * 拉取当前厂商的模型清单（`GET {baseUrl}/models`）。
     * 与 `embed()` 独立：不进入向量化队列，不改变索引状态。
     */
    void fetchModels();

signals:
    void finished(const EmbeddingResult &result);
    void failed(const ChatError &error);

    void validationChanged();
    /// 拉取完成：ids 为空时 error 必为明确中文原因；成功时 error 为空
    void modelsFetched(const QStringList &ids, const QString &error);

private:
    void issueNextBatch();
    void handleFinished(int handle);
    void handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure,
                      const QString &errorText);
    void fail(const ChatError &error);
    void finishSuccess();
    /// 解析一批响应；失败时写 error 并返回 false
    bool parseBatch(const QByteArray &body, int expectedCount, QString *error);

    void setValidation(ValidationStatus status, const QString &message);
    void finishValidationSuccess(const QByteArray &body);
    void failValidation(int httpStatus, NetworkTransport::Failure failure);
    void finishModelsFetch(const QByteArray &body);
    void failModelsFetch(int httpStatus, NetworkTransport::Failure failure);

    NetworkTransport *m_transport = nullptr;
    ProviderConfig m_provider;
    LibraryEmbeddingConfig m_embedding;

    bool m_busy = false;
    int m_handle = 0;
    QStringList m_inputs;
    int m_nextIndex = 0;
    QByteArray m_body;
    QList<QVector<float>> m_vectors;
    ChatUsage m_usage;
    int m_dimensions = 0;

    ValidationStatus m_validationStatus = Idle;
    QString m_validationMessage;
    int m_validationHandle = 0;
    QByteArray m_validationBody;
    int m_modelsHandle = 0;
    QByteArray m_modelsBody;
};
