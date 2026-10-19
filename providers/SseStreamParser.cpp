#include "providers/SseStreamParser.h"

QVector<SseEvent> SseStreamParser::feed(const QByteArray &chunk)
{
    QVector<SseEvent> events;
    m_buffer.append(chunk);

    int start = 0;
    int nl = -1;
    while ((nl = m_buffer.indexOf('\n', start)) != -1) {
        QByteArray line = m_buffer.mid(start, nl - start);
        start = nl + 1;
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.isEmpty()) {
            flushEvent(&events);
            continue;
        }
        if (line.startsWith(':'))
            continue;  // 注释 / 心跳行

        const int colon = line.indexOf(':');
        QByteArray field;
        QByteArray value;
        if (colon == -1) {
            field = line;  // 允许省略冒号的字段行
        } else {
            field = line.left(colon);
            value = line.mid(colon + 1);
            if (value.startsWith(' '))
                value.remove(0, 1);
        }

        if (field == "event") {
            m_event = QString::fromUtf8(value);
        } else if (field == "data") {
            if (m_hasData)
                m_data.append('\n');
            m_data.append(value);
            m_hasData = true;
        }
    }

    m_buffer.remove(0, start);
    return events;
}

QVector<SseEvent> SseStreamParser::finish()
{
    QVector<SseEvent> events;
    if (!m_buffer.isEmpty()) {
        const QByteArray tail = m_buffer + '\n';
        m_buffer.clear();
        events = feed(tail);
    }
    flushEvent(&events);
    return events;
}

void SseStreamParser::reset()
{
    m_buffer.clear();
    m_event.clear();
    m_data.clear();
    m_hasData = false;
}

void SseStreamParser::flushEvent(QVector<SseEvent> *out)
{
    if (m_hasData || !m_event.isEmpty()) {
        SseEvent event;
        event.event = m_event.isEmpty() ? QStringLiteral("message") : m_event;
        event.data = m_data;
        out->append(event);
    }
    m_event.clear();
    m_data.clear();
    m_hasData = false;
}
