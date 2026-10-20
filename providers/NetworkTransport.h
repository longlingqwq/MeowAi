#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * 网络传输抽象。
 *
 * providers/ 层只依赖本接口发起请求，从而可以：
 *  - 生产环境注入基于 QNetworkAccessManager 的实现（QNetworkTransport）；
 *  - 单测注入假实现，离线复现 401 / 429 / 超时 / 断网 / 流中断等异常流。
 *
 * 所有回调均为信号，天然异步；请求者凭 handle 区分并发请求。
 */
class NetworkTransport : public QObject
{
    Q_OBJECT

public:
    /// 失败分类，避免上层解析错误字符串
    enum class Failure {
        Other,
        Timeout,             ///< 超时
        NetworkUnavailable,  ///< 断网 / 无法连接
        Cancelled,           ///< 被本地 abort() 中断
    };

    struct Request
    {
        QString url;
        QString method = QStringLiteral("POST");  ///< POST / GET
        QList<QPair<QString, QString>> headers;
        QByteArray body;
        int timeoutMs = 0;  ///< 0 表示不设置显式超时
    };

    explicit NetworkTransport(QObject *parent = nullptr);
    ~NetworkTransport() override = default;

    /// 发起 POST，返回句柄（>0）；无法发起时返回 0
    virtual int post(const Request &request) = 0;

    /// 发起 GET（用于 `GET /models` 校验与模型清单拉取），返回句柄；无法发起时返回 0
    virtual int get(const Request &request) = 0;

    /// 中断在途请求；未知句柄为空操作。被中断的请求**不会**再发出 finished / failed
    virtual void abort(int handle) = 0;

signals:
    /// 收到响应头（含 HTTP 状态码）
    void headersReceived(int handle, int httpStatus);
    /// 收到一段响应体（流式场景会多次触发）
    void chunkReceived(int handle, const QByteArray &chunk);
    /// 请求正常结束（响应体已完整）
    void finished(int handle);
    /// 请求失败（含非 2xx 状态与网络错误）
    void failed(int handle, int httpStatus, NetworkTransport::Failure failure, const QString &errorText);
};

/**
 * 基于 QNetworkAccessManager 的生产实现。
 *
 * 仅负责"把请求发出去 / 把字节收回来"，不做任何协议解析；
 * 超时、断网等错误被归类为 NetworkTransport::Failure 后向上抛出。
 */
class QNetworkTransport : public NetworkTransport
{
    Q_OBJECT

public:
    explicit QNetworkTransport(QObject *parent = nullptr);

    int post(const Request &request) override;
    int get(const Request &request) override;
    void abort(int handle) override;

private:
    /// post / get 的公共实现：按 request.method 选择动词，其余处理完全一致
    int send(const Request &request);

    QNetworkAccessManager *m_manager = nullptr;
    QHash<int, QNetworkReply *> m_replies;
    int m_nextHandle = 1;
};
