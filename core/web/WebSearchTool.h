#pragma once

#include "core/web/WebSearchProvider.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class WebSearchService;

/**
 * 把联网检索包装成「模型可调用的工具」（B 形态，v0.2.4，core/web）。
 *
 * 职责边界（与 core/chat 严格分开）：
 *  - 本类**只管检索能力**：对外暴露工具描述（OpenAI 兼容 function schema），
 *    并把一次 `execute()` 的结果异步转成可回填给模型的文本；
 *  - **谁决定搜**由 core/chat 的对话循环负责：本类不感知对话上下文、不做轮数控制、不拼消息。
 *
 * 结果文本沿用 WebSearchService 已拼好的「资料上下文」，失败时回传 error 由上层决定话术。
 */
class WebSearchTool : public QObject
{
    Q_OBJECT

public:
    /// 工具名（模型请求里的 function name；对话循环据此分发）
    static QString toolName();

    /// OpenAI 兼容的工具描述（直接放入请求体 tools 数组）
    static QJsonObject descriptor();

    /// service 允许为 nullptr：此时 execute() 立即以 error 回传
    explicit WebSearchTool(WebSearchService *service, QObject *parent = nullptr);

    /// 注入生效的检索方案（透传给 WebSearchService）
    void configure(const WebSearchConfig &config);
    /// 清除运行时方案（回退默认路径）
    void clearConfig();

    bool configured() const;
    /// 面向用户的状态说明；空串表示就绪
    QString statusMessage() const;
    bool busy() const;

    /// 执行一次检索；结束于 finished（resultText 为可回填给模型的资料文本）
    void execute(const QString &query);
    /// 取消在途检索
    void cancel();

signals:
    /// resultText：可直接作为 tool 结果文本的资料内容（失败时为空）；
    /// sources：本轮命中的来源（供 UI 展示）；error：非空表示失败
    void finished(const QString &resultText, const QList<WebSearchResult> &sources,
                  const QString &error);

private:
    void onServiceFinished(const QString &context, const QList<WebSearchResult> &sources,
                           const QString &error);

    WebSearchService *m_service = nullptr;
    /// 仅当本类发起了一次 execute 时才接受 service 的回传（避免误接他人调用）
    bool m_running = false;
};
