#pragma once

#include "core/library/extract/Extractor.h"
#include "storage/LibraryStore.h"

#include <QList>
#include <QString>

/// 章节起点：解析层识别出的标题落在哪个片段序号上
struct HeadingSpan
{
    int ordinal = 0;   ///< 该标题首个片段在文件内的序号
    int level = 1;     ///< 标题层级（1–6）
    QString title;     ///< 标题文本（已去掉 Markdown `#` 前缀）
};

/**
 * 切分参数（来自 `LibraryConfigData::chunkSize` / `chunkOverlap`）。
 *
 * 两个值都可配置，且改动后**行为必须随之改变**（DoD 第 5 条）：
 * `chunkSize` 决定单片段字符上限，`overlap` 决定相邻片段的重叠字符数。
 */
struct ChunkSplitOptions
{
    int chunkSize = 700;
    int overlap = 120;

    /// 生效值：尺寸下限 40 字符，重叠不超过目标尺寸的一半（否则会原地打转）
    ChunkSplitOptions normalized() const;
};

/**
 * 粗略 token 估算（不引入分词依赖）：
 *  - CJK 字符按 **1 字 ≈ 1 token**；
 *  - 其余字符按 **4 字符 ≈ 1 token**。
 * 用于「token 预算」的保守估算与成本预估；宁多估、不少估。
 */
int estimateTokens(const QString &text);

/**
 * 片段切分器（模块 M4，纯本地、无网络、无持久化）。
 *
 * 输入是解析层产出的 `ExtractedSegment`（**已带定位元数据**），输出可直接交给
 * `LibraryStore::replaceChunks` 的 `ChunkRecord` 列表：
 *  - `ordinal` 在**文件内连续递增**（邻居扩展依赖连续性）；
 *  - `loc_*` 元数据逐条保留；表格被拆分时按数据行偏移重算行区间与定位串；
 *  - 表格片段**每块重复表头**，否则脱离表头的 `| 1200万 |` 无法理解行列含义；
 *  - 文本按字符窗口切分，切点尽量落在段落 / 句末，避免把一句话拦腰截断；
 *  - 相邻块重叠 `overlap` 字符（放不下时以目标尺寸优先，绝不产生超短片段）。
 */
class ChunkSplitter
{
public:
    explicit ChunkSplitter(ChunkSplitOptions options = ChunkSplitOptions());

    ChunkSplitOptions options() const;

    /// 切分；`fileId` 仅写入记录，本类不访问数据库。空片段被跳过。
    /// `outHeadings` 非空时收集标题起点（docx 样式标题 / markdown `#`），供章节切分使用
    QList<ChunkRecord> split(qint64 fileId, const QList<ExtractedSegment> &segments,
                             QList<HeadingSpan> *outHeadings = nullptr) const;

private:
    /// 纯文本 / 页面 / 幻灯片 / JSON 等：按字符窗口 + 段落边界切分
    void splitText(qint64 fileId, const ExtractedSegment &segment, QList<ChunkRecord> *out,
                   int *ordinal) const;
    /// 表格（Markdown 表格）：按行分组、每块重复表头
    void splitTable(qint64 fileId, const ExtractedSegment &segment, QList<ChunkRecord> *out,
                    int *ordinal) const;

    ChunkRecord makeChunk(qint64 fileId, int ordinal, const QString &text,
                          const ExtractedSegment &segment) const;

    ChunkSplitOptions m_options;
};
