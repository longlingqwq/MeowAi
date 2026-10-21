#include <QtTest>
#include "core/library/CostEstimator.h"

#include <QDir>

namespace {
QString pricingPath()
{
    return QDir(QStringLiteral(MEOWAI_SOURCE_DIR)).filePath(QStringLiteral("resources/pricing.json"));
}
} // namespace

class tst_cost : public QObject
{
    Q_OBJECT

private slots:
    void load_default_works();
    void unknown_returns_known_false();
    void known_provider_calculates_correctly();
    void zero_tokens_gives_zero();
};

void tst_cost::load_default_works()
{
    CostEstimator estimator;
    QString error;
    QVERIFY(estimator.load(pricingPath(), &error));
    QVERIFY(error.isEmpty());
    QVERIFY(estimator.isLoaded());
}

void tst_cost::unknown_returns_known_false()
{
    CostEstimator estimator;
    estimator.load(pricingPath());
    CostLine line = estimator.embeddingCost("not_exists", "no_model", 1000);
    QVERIFY(!line.known);
    QCOMPARE(line.tokens, 1000);
    QCOMPARE(line.amount, 0.0);
}

void tst_cost::known_provider_calculates_correctly()
{
    CostEstimator estimator;
    estimator.load(pricingPath());
    // openai text-embedding-3-small: $0.02 / 1M → 1000000 → $0.02
    const CostLine line = estimator.embeddingCost("openai", "text-embedding-3-small", 1000000);
    QVERIFY(line.known);
    QCOMPARE(line.tokens, 1000000);
    QCOMPARE(line.currency, QString("USD"));
    QVERIFY(qFuzzyCompare(line.amount, 0.02));
}

void tst_cost::zero_tokens_gives_zero()
{
    CostEstimator estimator;
    estimator.load(pricingPath());
    const CostLine line = estimator.embeddingCost("openai", "text-embedding-3-small", 0);
    QVERIFY(line.known);
    QCOMPARE(line.tokens, 0);
    QVERIFY(qFuzzyCompare(line.amount, 0.0));
}

QTEST_APPLESS_MAIN(tst_cost)

#include "tst_cost.moc"
