#include "core/library/CostEstimator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

CostEstimator::CostEstimator() = default;

QString CostEstimator::defaultResourcePath()
{
    return QStringLiteral(":/pricing/pricing.json");
}

bool CostEstimator::load(const QString &path, QString *error)
{
    m_loaded = false;
    m_embedding.clear();
    m_vision.clear();
    m_chat.clear();
    if (error)
        error->clear();

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("价格表不可读：%1").arg(path);
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("价格表已损坏：%1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    const auto parseList = [](const QJsonArray &array) {
        QList<Price> prices;
        for (const QJsonValue &entry : array) {
            const QJsonObject object = entry.toObject();
            Price price;
            price.providerId = object.value(QStringLiteral("providerId")).toString().trimmed();
            price.modelId = object.value(QStringLiteral("modelId")).toString().trimmed();
            if (price.providerId.isEmpty() || price.modelId.isEmpty())
                continue;
            price.currency = object.value(QStringLiteral("currency")).toString().trimmed();
            if (price.currency.isEmpty())
                price.currency = QStringLiteral("USD");
            price.perMTokens = object.value(QStringLiteral("perMTokens")).toDouble(0.0);
            price.tokensPerImage = object.value(QStringLiteral("tokensPerImage")).toInt(0);
            price.inputPerMTokens = object.value(QStringLiteral("inputPerMTokens")).toDouble(0.0);
            price.outputPerMTokens = object.value(QStringLiteral("outputPerMTokens")).toDouble(0.0);
            price.source = object.value(QStringLiteral("source")).toString().trimmed();
            prices.append(price);
        }
        return prices;
    };

    m_embedding = parseList(root.value(QStringLiteral("embedding")).toArray());
    m_vision = parseList(root.value(QStringLiteral("vision")).toArray());
    m_chat = parseList(root.value(QStringLiteral("chat")).toArray());

    m_loaded = true;
    return true;
}

bool CostEstimator::isLoaded() const
{
    return m_loaded;
}

const CostEstimator::Price *CostEstimator::find(const QList<Price> &prices,
                                                const QString &providerId,
                                                const QString &modelId)
{
    if (providerId.isEmpty() || modelId.isEmpty())
        return nullptr;
    for (const Price &price : prices) {
        if (price.providerId == providerId && price.modelId == modelId)
            return &price;
    }
    return nullptr;
}

double CostEstimator::amountFor(const Price &price, int tokens)
{
    if (tokens <= 0 || price.perMTokens <= 0.0)
        return 0.0;
    return tokens / 1000000.0 * price.perMTokens;
}

CostLine CostEstimator::embeddingCost(const QString &providerId, const QString &modelId,
                                      int tokens) const
{
    CostLine line;
    line.tokens = qMax(0, tokens);
    const Price *price = find(m_embedding, providerId, modelId);
    if (!price)
        return line;
    line.known = true;
    line.amount = amountFor(*price, line.tokens);
    line.currency = price->currency;
    line.source = price->source;
    return line;
}

CostLine CostEstimator::visionCost(const QString &providerId, const QString &modelId,
                                   int images) const
{
    CostLine line;
    line.images = qMax(0, images);
    const Price *price = find(m_vision, providerId, modelId);
    if (!price)
        return line;
    line.known = true;
    line.tokens = line.images * qMax(0, price->tokensPerImage);
    line.amount = amountFor(*price, line.tokens);
    line.currency = price->currency;
    line.source = price->source;
    return line;
}

CostLine CostEstimator::chatCost(const QString &providerId, const QString &modelId,
                                 int inputTokens, int outputTokens) const
{
    CostLine line;
    line.tokens = qMax(0, inputTokens);
    const Price *price = find(m_chat, providerId, modelId);
    if (!price)
        return line;
    line.known = true;
    line.amount = line.tokens / 1000000.0 * price->inputPerMTokens
        + qMax(0, outputTokens) / 1000000.0 * price->outputPerMTokens;
    line.currency = price->currency;
    line.source = price->source;
    return line;
}
