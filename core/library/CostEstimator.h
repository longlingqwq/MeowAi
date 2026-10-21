#pragma once

#include <QList>
#include <QString>

/**
 * 一条成本估算（模块 M8）。
 *
 * `known == false` 表示该「厂商 + 模型」未被 `pricing.json` 收录：此时仍给出 token 估算，
 * 但不编造金额（UI 显示「未收录单价」），避免误导用户。
 */
struct CostLine
{
    bool known = false;
    int tokens = 0;        ///< 计费 token（embedding / chat 输入）
    int images = 0;        ///< 图片张数（vision）
    double amount = 0.0;   ///< 金额；unknown 时为 0
    QString currency;      ///< USD / CNY（来自价格表）
    QString source;        ///< 官方价格页 URL，便于用户自行核对
};

/**
 * 成本预估（模块 M8）：**纯本地查表**，不发请求、不读用户文件。
 *
 * 价格数据来自内嵌 `resources/pricing.json`（随版本更新，出处逐条写在表内）。
 * 查找失败、JSON 损坏、模型未收录一律降级为 `known == false`，绝不崩。
 * 金额 = token 数 ÷ 1,000,000 × 每百万 token 单价；vision 按「每张图 token × 张数」折算。
 */
class CostEstimator
{
public:
    CostEstimator();

    /// 载入价格表；失败写 error 并保持「未加载」状态（诚实降级）
    bool load(const QString &path = defaultResourcePath(), QString *error = nullptr);
    bool isLoaded() const;

    static QString defaultResourcePath();

    CostLine embeddingCost(const QString &providerId, const QString &modelId, int tokens) const;
    CostLine visionCost(const QString &providerId, const QString &modelId, int images) const;
    CostLine chatCost(const QString &providerId, const QString &modelId, int inputTokens,
                      int outputTokens = 0) const;

private:
    struct Price
    {
        QString providerId;
        QString modelId;
        QString currency;
        double perMTokens = 0.0;      ///< embedding / vision 的 token 单价
        int tokensPerImage = 0;       ///< vision：每张图折算 token
        double inputPerMTokens = 0.0; ///< chat 输入单价
        double outputPerMTokens = 0.0;///< chat 输出单价
        QString source;
    };

    static double amountFor(const Price &price, int tokens);
    static const Price *find(const QList<Price> &prices, const QString &providerId,
                             const QString &modelId);

    bool m_loaded = false;
    QList<Price> m_embedding;
    QList<Price> m_vision;
    QList<Price> m_chat;
};
