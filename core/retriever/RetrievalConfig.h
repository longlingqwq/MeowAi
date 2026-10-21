#pragma once

#include "core/retriever/DocumentRetriever.h"

#include <QList>
#include <QString>

/**
 * 检索配置（三项**用户可配置且改动后行为必须改变**，见 DoD 第 5 条）。
 * 取值来自 `LibraryConfigData`，由上层注入，检索层不读配置文件。
 */
struct RetrievalConfig
{
    int topK = 6;                    ///< 最终注入 prompt 的命中片段数上限
    int perFileLimit = 2;            ///< 每文件片段上限：防止单个大文件占满 Top-K
    int tokenBudget = 2000;          ///< 注入上下文的 token 预算（保守估算）
    double similarityThreshold = 0.30;  ///< 余弦相似度阈值：低于此值的向量命中丢弃
    int neighborRadius = 1;          ///< 邻居扩展半径（命中片段前后各带几段）
    bool ftsFallback = true;         ///< embedding 不可用时降级为关键词检索
    int maxCandidates = 60;          ///< 粗排候选上限（向量 / 关键词各自内部使用）
};

/**
 * 一次检索的结果与**可观测信息**（对话页要展示命中数 / token 占用 / 是否降级）。
 * 所有字段均不含密钥或完整请求体。
 */
struct RetrievalResult
{
    QList<RetrievedChunk> chunks;  ///< 已按展示顺序排好（命中片段 + 前后邻居）
    int vectorHits = 0;            ///< 过阈值且过每文件上限前的向量命中数
    int keywordHits = 0;           ///< 合并前关键词命中数
    int tokenEst = 0;              ///< 实际占用（保守估算）
    QString mode;                  ///< "vector" | "hybrid" | "keyword"
    bool degraded = false;         ///< true = 退化为关键词检索
    QString note;                  ///< 面向用户的降级说明（如「未配置 embedding…」）
};
