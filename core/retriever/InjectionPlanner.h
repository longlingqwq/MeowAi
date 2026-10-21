#pragma once

#include <QList>
#include <QString>

#include "core/retriever/SectionPicker.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryStore.h"

/// 注入级别：全文 → 章节 → 片段（逐级降级）
enum class InjectionLevel {
    None,      ///< 不注入
    FullText,  ///< 全文注入
    Section,   ///< 章节注入
    Fragment,  ///< 片段注入（回退）
};

struct InjectionResult
{
    InjectionLevel level = InjectionLevel::None;
    QString content;
    int tokenEst = 0;
    int originalTokenEst = 0;  ///< 文件原始大小，用于展示截断比例
    bool truncated = false;
    QString trace;             ///< 降级链路说明（中文）
    QList<SelectedSection> selectedSections;
};

class DocumentStore;
class FullTextAssembler;
class LibraryStore;
class SectionPicker;

/**
 * 单文件注入策略决策：全文 → 章节 → 片段。
 * 每一级降级都会在 `InjectionResult::trace` 留下中文说明，由 UI 展示。
 */
class InjectionPlanner
{
public:
    InjectionPlanner(LibraryStore *libraryStore, DocumentStore *docStore,
                     FullTextAssembler *fullText, SectionPicker *sectionPicker);

    /**
     * @param chunkScores      该文件命中的片段分数（用于章节排序与片段回退）
     * @param effectivePerFile 该文件可用预算（已由 BudgetCalculator 钳制）
     * @param overlap          切分重叠字符数（拼接去重用）
     * @param enableSections   是否有章节结构（无结构则跳过章节级）
     */
    InjectionResult plan(qint64 fileId, const QList<ChunkScore> &chunkScores, int effectivePerFile,
                         int overlap, bool enableSections) const;

private:
    /// 章节级注入：按文件内顺序拼接选中章节，超出预算的章节跳过
    QString buildSectionContent(qint64 fileId, const QList<SectionRecord> &sections, int overlap,
                                int effectivePerFile, int *outTokens, int *outSkipped) const;

    /// 片段级回退：按分数降序装入片段，直到预算用尽
    QString buildFragmentContent(const QList<ChunkRecord> &allChunks,
                                 const QList<ChunkScore> &chunkScores, int effectivePerFile,
                                 int *outTokens) const;

    LibraryStore *m_libraryStore = nullptr;
    DocumentStore *m_docStore = nullptr;
    FullTextAssembler *m_fullText = nullptr;
    SectionPicker *m_sectionPicker = nullptr;
};
