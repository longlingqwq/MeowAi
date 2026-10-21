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

signals:
    void finished(const EmbeddingResult &result);
    void failed(const ChatError &error);

private:
    void issueNextBatch();
    void handleFinished(int handle);
    void handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure,
                      const QString &errorText);
    void fail(const ChatError &error);
    void finishSuccess();
    /// 解析一批响应；失败时写 error 并返回 false
    bool parseBatch(const QByteArray &body, int expectedCount, QString *error);

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
};
