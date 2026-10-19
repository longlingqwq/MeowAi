#pragma once

#include "providers/AiProvider.h"

#include <QObject>
#include <QString>

#include <memory>

class SecureStore;

/**
 * 对话控制器（core 层）。
 *
 * 职责边界：
 *  - API Key 只从 storage/SecureStore 读取，**绝不写入配置或日志**；
 *  - 组装 ChatRequest 并交给 providers/ 层发送；
 *  - 图片上传必须经用户**显式确认**（imagesConfirmed），否则由 Provider 拒绝；
 *  - 向 UI 暴露流式增量与结构化错误，自身不含任何网络/协议细节。
 */
class ChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString providerId READ providerId NOTIFY providerChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString replyText READ replyText NOTIFY replyTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY apiKeyStateChanged)
    Q_PROPERTY(bool dataStoreAvailable READ dataStoreAvailable NOTIFY apiKeyStateChanged)
    Q_PROPERTY(bool imagesConfirmed READ imagesConfirmed WRITE setImagesConfirmed NOTIFY imagesConfirmedChanged)

public:
    explicit ChatController(SecureStore *store, QObject *parent = nullptr);
    ~ChatController() override;

    QString providerId() const;
    QString model() const;
    void setModel(const QString &model);
    bool busy() const;
    QString replyText() const;
    QString lastError() const;
    bool hasApiKey() const;
    bool dataStoreAvailable() const;
    bool imagesConfirmed() const;
    void setImagesConfirmed(bool confirmed);

    /// 切换 Provider（"openai" / "anthropic"），会重建 Provider 并重置默认模型
    Q_INVOKABLE void setProvider(const QString &providerId);
    /// 通过 SecureStore 加密保存当前 Provider 的 Key；失败返回 false 并写 lastError
    Q_INVOKABLE bool saveApiKey(const QString &apiKey);
    Q_INVOKABLE bool eraseApiKey();
    Q_INVOKABLE void send(const QString &text);
    /// 图片仅在 imagesConfirmed 为 true 时才会真正上传
    Q_INVOKABLE void sendWithImage(const QString &text, const QString &imagePath);
    Q_INVOKABLE void cancel();

signals:
    void providerChanged();
    void modelChanged();
    void busyChanged();
    void replyTextChanged();
    void lastErrorChanged();
    void apiKeyStateChanged();
    void imagesConfirmedChanged();

private:
    void ensureProvider();
    void sendRequest(const ChatRequest &request);
    void setBusy(bool busy);
    void setLastError(const QString &message);

    SecureStore *m_store = nullptr;
    std::unique_ptr<AiProvider> m_provider;
    QString m_providerId = QStringLiteral("openai");
    QString m_model;
    bool m_busy = false;
    QString m_replyText;
    QString m_lastError;
    bool m_imagesConfirmed = false;
};
