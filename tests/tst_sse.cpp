#include "providers/SseStreamParser.h"

#include <QtTest>

/**
 * SseStreamParser 单元测试：覆盖分片、多行 data、注释行、CRLF 与收尾。
 */
class TstSse : public QObject
{
    Q_OBJECT

private slots:
    void singleEvent();
    void eventNameAndData();
    void multilineData();
    void splitAcrossChunks();
    void ignoresComments();
    void crlfLineEndings();
    void finishFlushesPending();
    void resetClearsState();
};

void TstSse::singleEvent()
{
    SseStreamParser parser;
    const QVector<SseEvent> events = parser.feed("data: {\"a\":1}\n\n");

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).event, QStringLiteral("message"));
    QCOMPARE(events.at(0).data, QByteArray("{\"a\":1}"));
}

void TstSse::eventNameAndData()
{
    SseStreamParser parser;
    const QVector<SseEvent> events = parser.feed("event: content_block_delta\ndata: {\"x\":2}\n\n");

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).event, QStringLiteral("content_block_delta"));
    QCOMPARE(events.at(0).data, QByteArray("{\"x\":2}"));
}

void TstSse::multilineData()
{
    SseStreamParser parser;
    const QVector<SseEvent> events = parser.feed("data: line1\ndata: line2\n\n");

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("line1\nline2"));
}

void TstSse::splitAcrossChunks()
{
    SseStreamParser parser;
    QCOMPARE(parser.feed("eve").size(), 0);

    const QVector<SseEvent> events = parser.feed("nt: ping\ndata: hello\n\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).event, QStringLiteral("ping"));
    QCOMPARE(events.at(0).data, QByteArray("hello"));
}

void TstSse::ignoresComments()
{
    SseStreamParser parser;
    QCOMPARE(parser.feed(": keepalive\n\n").size(), 0);

    const QVector<SseEvent> events = parser.feed(": note\ndata: real\n\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("real"));
}

void TstSse::crlfLineEndings()
{
    SseStreamParser parser;
    const QVector<SseEvent> events = parser.feed("data: hi\r\n\r\n");

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("hi"));
}

void TstSse::finishFlushesPending()
{
    SseStreamParser parser;
    QCOMPARE(parser.feed("data: tail").size(), 0);

    const QVector<SseEvent> events = parser.finish();
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("tail"));
}

void TstSse::resetClearsState()
{
    SseStreamParser parser;
    parser.feed("data: x\n");
    parser.reset();

    QCOMPARE(parser.finish().size(), 0);
}

QTEST_GUILESS_MAIN(TstSse)
#include "tst_sse.moc"
