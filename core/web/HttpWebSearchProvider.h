#pragma once

#include "core/web/WebSearchProvider.h"
#include "providers/NetworkTransport.h"

#include <QByteArray>
#include <QString>

/**
 * 联网检索的 HTTP 直连实现，支持两种后端协议（v0.2.4）：
 *
 *  - Generic（自填端点）：
 *      搜索 `GET <endpoint>`（`{query}` 占位符或自动追加 `?q=`），期望返回 JSON 结果数组；
 *      抓取 `GET <url>`，抽取 `<title>` 与去标签正文并裁剪。
 *  - DeepSeek（厂商内置，POST JSON，鉴权复用厂商 Key）：
 *      搜索 `POST <endpoint>`，body `{"query","count"}`，响应 `{"results":[{title,url,summary,…}]}`；
 *      抓取 `POST <fetchEndpoint>`，body `{"url"}`，响应 `{"title","content"(Markdown)}`。
 *
 *  - 网络一律经 `NetworkTransport` 发出（生产用 QNetworkTransport，单测注入假实现）。
 */
class HttpWebSearchProvider : public WebSearchProvider
{
    Q_OBJECT

public:
    /// transport 为空时自建生产实现；测试可注入 FakeNetworkTransport
    explicit HttpWebSearchProvider(NetworkTransport *transport = nullptr, QObject *parent = nullptr);

    QString backendId() const override;
    void configure(const WebSearchConfig &config) override;

    void search(const QString &query, int topK) override;
    void fetch(const QString &url) override;
    void cancel() override;

private:
    enum class Mode { None, Search, Fetch };

    void handleChunk(int handle, const QByteArray &chunk);
    void handleFinished(int handle);
    void handleFailed(int handle, int httpStatus, NetworkTransport::Failure failure,
                      const QString &errorText);
    void failCurrent(const QString &message);

    /// 是否为 DeepSeek 内置协议
    bool isDeepSeek() const;
    QString buildSearchUrl(const QString &query) const;
    NetworkTransport::Request requestFor(const QString &url) const;
    /// POST JSON 请求（DeepSeek 协议用）：自动带 Content-Type / 鉴权头
    NetworkTransport::Request jsonRequest(const QString &url, const QByteArray &body) const;
    QList<WebSearchResult> parseResults() const;
    WebFetchResult parseFetch() const;

    NetworkTransport *m_transport = nullptr;
    WebSearchConfig m_config;
    Mode m_mode = Mode::None;
    int m_handle = 0;
    QByteArray m_body;
    QString m_pageUrl;
    int m_topK = 5;
};
