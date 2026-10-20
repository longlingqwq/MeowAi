#include "providers/NetworkTransport.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

NetworkTransport::NetworkTransport(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// QNetworkTransport
// ---------------------------------------------------------------------------

QNetworkTransport::QNetworkTransport(QObject *parent)
    : NetworkTransport(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

int QNetworkTransport::post(const Request &request)
{
    Request postRequest = request;
    postRequest.method = QStringLiteral("POST");
    return send(postRequest);
}

int QNetworkTransport::get(const Request &request)
{
    Request getRequest = request;
    getRequest.method = QStringLiteral("GET");
    return send(getRequest);
}

int QNetworkTransport::send(const Request &request)
{
    if (!m_manager || request.url.isEmpty())
        return 0;

    QNetworkRequest netRequest((QUrl(request.url)));
    netRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                           QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &header : request.headers)
        netRequest.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
    if (request.timeoutMs > 0)
        netRequest.setTransferTimeout(request.timeoutMs);

    const bool useGet = request.method.compare(QStringLiteral("GET"), Qt::CaseInsensitive) == 0;
    QNetworkReply *reply = useGet ? m_manager->get(netRequest)
                                  : m_manager->post(netRequest, request.body);
    const int handle = m_nextHandle++;
    m_replies.insert(handle, reply);

    connect(reply, &QIODevice::readyRead, this, [this, handle, reply]() {
        emit chunkReceived(handle, reply->readAll());
    });

    connect(reply, &QNetworkReply::finished, this, [this, handle, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!m_replies.contains(handle)) {
            // 已被 abort()：不向调用方抛任何结果
            reply->deleteLater();
            return;
        }
        m_replies.remove(handle);

        switch (reply->error()) {
        case QNetworkReply::NoError:
            emit finished(handle);
            break;
        case QNetworkReply::OperationCanceledError:
            emit failed(handle, status, Failure::Cancelled, reply->errorString());
            break;
        case QNetworkReply::TimeoutError:
            emit failed(handle, status, Failure::Timeout, reply->errorString());
            break;
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::UnknownNetworkError:
            emit failed(handle, status, Failure::NetworkUnavailable, reply->errorString());
            break;
        default:
            // 非 2xx 也会走到这里：交给上层按 httpStatus 判定 401 / 429 / 5xx
            emit failed(handle, status, Failure::Other, reply->errorString());
            break;
        }
        reply->deleteLater();
    });

    return handle;
}

void QNetworkTransport::abort(int handle)
{
    QNetworkReply *reply = m_replies.value(handle, nullptr);
    if (!reply)
        return;

    m_replies.remove(handle);
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}
