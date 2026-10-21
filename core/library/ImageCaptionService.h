#pragma once

#include "providers/ChatTypes.h"
#include "storage/LibraryConfigStore.h"

#include <QObject>
#include <QQueue>
#include <QString>

class AiProvider;

/**
 * 图片 caption 服务（模块 M3-d）。
 *
 * 把「本地已转码」的图片经**既有 `AiProvider`** 送视觉模型，一次调用拿到
 * 「描述 + 强制文字提取」，回填为可入库文本。**不新建视觉 Provider**（复用同一套协议实现）。
 *
 * 分层与安全边界：
 *  - provider 由调用方（M4 索引器 / M7 控制器）用 `ProviderFactory` 建好、按 **caption 的
 *    providerId / modelId** 配置后注入；本服务**不读 SecureStore、不碰持久化、不落盘**；
 *  - **图片外发必须用户显式同意**：`consentGiven()` 为 false 时**一个请求都不发**，
 *    直接回 `ChatError::ImagesNotConfirmed`；
 *  - 错误一律结构化回传（429 / 超时 / 断网 / 401…），由上层决定「待索引」与重试，
 *    **不得**把限流或断网误判成「图片无效」；
 *  - 不打印图片内容、API Key 或完整请求体。
 *
 * 队列：FIFO 逐张处理（provider 单实例同一时刻只承载一个在途请求），单张失败不阻断后续。
 */
class ImageCaptionService : public QObject
{
    Q_OBJECT

public:
    explicit ImageCaptionService(AiProvider *provider, QObject *parent = nullptr);

    /// caption 运行参数（长边上限 / 最大输出 token / 提示词档位），直接复用资料库配置
    void setCaptionConfig(const LibraryCaptionConfig &config);
    LibraryCaptionConfig captionConfig() const;

    /// 用户对「图片外发」的显式同意开关；**默认 false**
    void setConsentGiven(bool given);
    bool consentGiven() const;

    /// provider 已配置且已指定 caption 模型（不含同意开关，同意与否单独判定并单独报错）
    bool isReady() const;

    /// 队列已空且无在途请求
    bool isIdle() const;

    /**
     * 入队一张图片：本地转码 → 视觉模型一次调用。
     * `fileId` 仅用于回传配对（本服务不落库）；`imageOrdinal` 用于定位串「图片 #N」。
     */
    void captionItem(qint64 fileId, const QString &path, int imageOrdinal = 1);

    /// 清空队列并中断在途请求（在途请求会以 `Cancelled` 回传，供上层标记待索引）
    void cancelAll();

signals:
    /// 成功：`caption` 为模型返回的可入库文本（已 trim）
    void captionReady(qint64 fileId, int imageOrdinal, const QString &caption, const ChatUsage &usage);
    /// 失败：结构化原因；`ImagesNotConfirmed` 表示被「用户未同意」拦住
    void captionFailed(qint64 fileId, const ChatError &error);
    /// 队列处理完毕（成功或失败均计）；`cancelAll()` 清空队列时也会触发
    void queueDrained();

private slots:
    void processNext();
    void onProviderFinished(const ChatResponse &response);
    void onProviderFailed(const ChatError &error);

private:
    struct Item
    {
        qint64 fileId = 0;
        QString path;
        int imageOrdinal = 1;
    };

    void failCurrent(const ChatError &error);
    void finishCurrent();

    AiProvider *m_provider = nullptr;
    LibraryCaptionConfig m_caption;  ///< 默认 maxEdge = 1536（`LibraryCaptionConfig` 默认值）
    bool m_consentGiven = false;

    QQueue<Item> m_queue;
    Item m_current;
    bool m_busy = false;
};
