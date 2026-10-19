#include "core/retriever/NullRetriever.h"

#include <QtTest>

/**
 * 检索接口占位测试：阶段一固定返回空结果，且不得因空查询/越界参数崩溃。
 */
class TstRetriever : public QObject
{
    Q_OBJECT

private slots:
    void backendIdIsNull();
    void returnsEmptyResultWithoutError();
    void toleratesDegenerateInput();
};

void TstRetriever::backendIdIsNull()
{
    NullRetriever retriever;
    QCOMPARE(retriever.backendId(), QStringLiteral("null"));
}

void TstRetriever::returnsEmptyResultWithoutError()
{
    NullRetriever retriever;
    QString error = QStringLiteral("dirty");
    const QList<RetrievedChunk> chunks = retriever.retrieve(QStringLiteral("任意查询"), 5, &error);
    QVERIFY(chunks.isEmpty());
    QVERIFY(error.isEmpty());
}

void TstRetriever::toleratesDegenerateInput()
{
    NullRetriever retriever;
    QVERIFY(retriever.retrieve(QString(), 0, nullptr).isEmpty());
    QVERIFY(retriever.retrieve(QStringLiteral("x"), -1, nullptr).isEmpty());
}

QTEST_GUILESS_MAIN(TstRetriever)
#include "tst_retriever.moc"
