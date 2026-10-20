#pragma once

#include "core/web/WebSearchProvider.h"

#include <QList>
#include <QObject>
#include <QString>

class SecureStore;
class WebConfigStore;

/**
 * 联网检索编排（v0.2.3 新模块，core 层）。
 *
 * 一次 `run()` 的编排：**检索 → 抓取前若干条网页正文 → 拼成可注入的上下文文本**，
 * 结果经 `finished` 回传。任一步失败都**降级**（保留已有摘要 / 或直接给出中文原因），
 * 绝不抛异常、不崩溃，由上层决定「继续按普通对话回答」。
 *
 * 依赖只经两个 storage 组件：端点配置走 WebConfigStore、密钥走 SecureStore。
 */
class WebSearchService : public QObject
{
    Q_OBJECT

public:
    /// provider 为**纯接口**依赖，具体后端由装配方注入（生产用 HttpWebSearchProvider）
    WebSearchService(WebSearchProvider *provider, SecureStore *secureStore,
                     WebConfigStore *configStore, QObject *parent = nullptr);

    /// 是否已具备检索条件（端点已填且密钥已存）
    bool configured() const;
    /// 面向用户的配置状态说明；返回空串表示已就绪，否则为需展示的中文提示
    QString statusMessage() const;
    bool busy() const;

    /// 运行时注入检索方案（由调用方按当前厂商解析后下发，v0.2.4）。
    /// 设置后 statusMessage() / run() 均以它为准，可支撑「厂商内置搜索、复用厂商 Key」的零配置路径。
    void configure(const WebSearchConfig &config);
    /// 清空运行时方案，回退到 WebConfigStore + SecureStore(webSearchApiKeyId) 的默认路径
    void clearRuntimeConfig();

    /// 发起一轮「检索 + 抓取 + 拼接」，异步结束于 finished
    void run(const QString &query);
    /// 取消在途检索（此后到达的结果一律被丢弃）
    void cancel();

signals:
    /// context 为可注入的上下文文本（失败时为空）；sources 为本轮命中的来源；error 非空表示失败
    void finished(const QString &context, const QList<WebSearchResult> &sources,
                  const QString &error);

private:
    void onSearchFinished(const QList<WebSearchResult> &results, const QString &error);
    void onFetchFinished(const WebFetchResult &result, const QString &error);
    void startNextFetch();
    void assembleAndFinish();
    void finishWithError(const QString &message);
    QString buildContext() const;

    WebSearchProvider *m_provider = nullptr;
    SecureStore *m_secureStore = nullptr;
    WebConfigStore *m_configStore = nullptr;

    bool m_running = false;
    /// 运行时检索方案（由调用方注入）；m_hasRuntimeConfig 为 true 时优先于 config/secure 存储
    WebSearchConfig m_runtimeConfig;
    bool m_hasRuntimeConfig = false;
    QString m_query;
    QList<WebSearchResult> m_results;
    QList<WebFetchResult> m_fetched;
    int m_fetchIndex = 0;
};
