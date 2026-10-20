#pragma once

#include <QList>
#include <QObject>
#include <QString>

/**
 * 联网检索（公网搜索 / 网页抓取）的公共数据结构。
 *
 * **与资料库检索刻意分开**：`core/retriever/DocumentRetriever` 面向「本地资料库」，
 * 本组类型面向「公网」，两者不共用类型与实现，避免职责杂糅。
 */

/// 一次搜索命中的结果条目
struct WebSearchResult
{
    QString title;
    QString url;
    QString snippet;
};

/// 抓取并抽取出的网页正文
struct WebFetchResult
{
    QString url;
    QString title;
    QString text;  ///< 已裁剪至长度上限的纯文本正文
};

/// 检索端点配置；**apiKey 只在内存中持有，绝不写日志、绝不落盘**（落盘只走 SecureStore）
struct WebSearchConfig
{
    /// 后端协议：Generic（自填通用搜索端点，GET）| DeepSeek（厂商内置，POST JSON）
    enum class Protocol { Generic, DeepSeek };

    QString endpoint;             ///< 搜索端点；Generic 可含 `{query}` 占位符，否则自动追加 `?q=`；DeepSeek 走 POST
    QString apiKey;               ///< 搜索服务密钥，为空时不携带鉴权头
    int timeoutMs = 15000;        ///< 单次请求超时
    int maxFetchChars = 6000;     ///< 网页正文裁剪上限，避免把整页塞进上下文

    Protocol protocol = Protocol::Generic;  ///< 后端协议（默认通用）
    QString fetchEndpoint;        ///< 网页抓取端点；DeepSeek 必填（POST {"url"}），Generic 为空表示直接 GET url
};

/**
 * 联网检索接口（core/web）。
 *
 * 设计约束：
 *  - **异步**：网络一律经 `providers/NetworkTransport` 抽象（其本身即异步），
 *    因此本接口**不提供阻塞式方法**，绝不阻塞 UI 线程；
 *  - **失败不抛异常**：一律以 `error` 回传，调用方据此降级；
 *  - 具体后端（HTTP 直连）由 `HttpWebSearchProvider` 实现，可注入假实现离线单测。
 */
class WebSearchProvider : public QObject
{
    Q_OBJECT

public:
    explicit WebSearchProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~WebSearchProvider() override = default;

    /// 后端标识（如 "http"）
    virtual QString backendId() const = 0;

    /// 注入检索端点（每轮检索前由 WebSearchService 写入）
    virtual void configure(const WebSearchConfig &config) = 0;

    /// 关键字检索；结果经 searchFinished 回传
    virtual void search(const QString &query, int topK) = 0;

    /// 抓取网页并抽取正文；结果经 fetchFinished 回传
    virtual void fetch(const QString &url) = 0;

    /// 中断在途请求（静默：被中断的请求**不再发出任何信号**）
    virtual void cancel() = 0;

signals:
    /// 检索结果；失败时 results 为空、error 非空
    void searchFinished(const QList<WebSearchResult> &results, const QString &error);
    /// 抓取结果；失败时 result.text 为空、error 非空
    void fetchFinished(const WebFetchResult &result, const QString &error);
};
