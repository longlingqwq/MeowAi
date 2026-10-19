#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

/// 单条 SSE 事件（对应协议中的 event / data 字段）
struct SseEvent
{
    QString event;    ///< event 名；协议未给定时为 "message"
    QByteArray data;  ///< data 内容；多行 data 以 '\n' 拼接
};

/**
 * Server-Sent Events 增量解析器（不依赖具体厂商协议）。
 *
 * 遵循 W3C SSE 基本规则：以空行分隔事件，行为 `field: value`，
 * 以 ':' 开头的行为注释（心跳），本阶段忽略 id / retry 字段。
 * 网络分片可能把一行拆开，故内部缓冲未完成的行。
 */
class SseStreamParser
{
public:
    /// 追加一个网络分片，返回本次完整解析出的事件（可能为空）
    QVector<SseEvent> feed(const QByteArray &chunk);

    /// 请求结束时调用：处理可能残留的未终止事件
    QVector<SseEvent> finish();

    void reset();

private:
    void flushEvent(QVector<SseEvent> *out);

    QByteArray m_buffer;   ///< 尚未遇到换行的残留数据
    QString m_event;       ///< 当前事件名
    QByteArray m_data;     ///< 当前事件 data
    bool m_hasData = false;
};
