#pragma once

#include "providers/NetworkTransport.h"

/**
 * 测试用假传输：不发任何真实请求。
 * post() 只登记请求并返回句柄；由测试显式调用 push* 驱动响应/错误，
 * 从而离线复现 401 / 429 / 超时 / 断网 / 流中断 / 空响应等异常流。
 */
class FakeNetworkTransport : public NetworkTransport
{
    Q_OBJECT

public:
    using NetworkTransport::NetworkTransport;

    int post(const Request &request) override
    {
        m_requests.append(request);
        m_lastHandle = ++m_handleCounter;
        return m_lastHandle;
    }

    void abort(int handle) override
    {
        m_abortedHandles.append(handle);
    }

    void pushChunk(int handle, const QByteArray &chunk) { emit chunkReceived(handle, chunk); }
    void pushFinished(int handle) { emit finished(handle); }
    void pushFailed(int handle, int httpStatus, Failure failure, const QString &text)
    {
        emit failed(handle, httpStatus, failure, text);
    }

    QList<Request> m_requests;
    QList<int> m_abortedHandles;
    int m_lastHandle = 0;

private:
    int m_handleCounter = 0;
};
