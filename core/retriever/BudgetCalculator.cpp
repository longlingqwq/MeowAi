#include "core/retriever/BudgetCalculator.h"

#include <QString>

BudgetCalculator::Output BudgetCalculator::calculate(const Input &input)
{
    Output out;

    int contextWindow = input.modelContextWindow;
    int fallback = input.fallbackContextWindow;
    if (fallback <= 0) {
        fallback = kDefaultContextWindow;
    }
    if (contextWindow <= 0) {
        contextWindow = fallback;
        out.contextWindowUnknown = true;
    }

    const int remaining = contextWindow - input.historyTokenEstimate - input.answerReserve;

    if (remaining <= 0) {
        out.effectiveGlobal = 0;
        out.effectivePerFile = 0;
        out.willDegrade = true;
        out.degradeReason = QStringLiteral(
            "上下文窗口已被历史消息占满，资料库内容无法注入");
        return out;
    }

    out.effectiveGlobal = qMin(input.userGlobalBudget, remaining);
    out.effectivePerFile = qMin(input.userPerFileBudget, out.effectiveGlobal);

    if (out.effectivePerFile < input.userPerFileBudget) {
        out.willDegrade = true;
        out.degradeReason = QStringLiteral(
            "因上下文窗口限制，单文件注入预算已从 %1 降至 %2")
            .arg(input.userPerFileBudget)
            .arg(out.effectivePerFile);
    } else if (out.effectiveGlobal < input.userGlobalBudget) {
        out.willDegrade = true;
        out.degradeReason = QStringLiteral(
            "因上下文窗口限制，全局注入预算已从 %1 降至 %2")
            .arg(input.userGlobalBudget)
            .arg(out.effectiveGlobal);
    }

    if (out.contextWindowUnknown) {
        if (!out.degradeReason.isEmpty()) {
            out.degradeReason += QStringLiteral("；");
        }
        out.degradeReason += QStringLiteral(
            "当前模型上下文窗口未知，按估算值 %1 计算").arg(fallback);
        out.willDegrade = true;
    }

    return out;
}
