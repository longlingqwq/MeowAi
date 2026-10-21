#pragma once

#include <QString>

/**
 * 上下文窗口预算计算器（纯函数组件，无状态无外部依赖）。
 *
 * 输入：模型 contextWindow + 历史消息估算 + 用户设定预算 + 回答预留
 * 输出：实际上限 + 是否会降级 + 降级原因
 */
struct BudgetCalculator
{
    /// 兜底默认值：用户未设定「未知窗口估算值」时使用
    static constexpr int kDefaultContextWindow = 16384;

    struct Input
    {
        int modelContextWindow = 0;        ///< 0 / 负数表示未知，回退到 fallbackContextWindow
        int fallbackContextWindow = 0;     ///< 用户设定的未知窗口估算值；≤0 时用 kDefaultContextWindow
        int historyTokenEstimate = 0;
        int userPerFileBudget = 20000;
        int userGlobalBudget = 100000;
        int answerReserve = 2000;
    };

    struct Output
    {
        int effectivePerFile = 0;
        int effectiveGlobal = 0;
        bool contextWindowUnknown = false;
        bool willDegrade = false;
        QString degradeReason;  ///< 中文降级说明（空字符串表示不会降级）
    };

    static Output calculate(const Input &input);
};
