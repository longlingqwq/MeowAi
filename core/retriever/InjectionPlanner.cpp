#include "core/retriever/InjectionPlanner.h"

#include "core/retriever/FullTextAssembler.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryStore.h"

#include <QHash>
#include <QSet>
#include <algorithm>

InjectionPlanner::InjectionPlanner(LibraryStore *libraryStore, DocumentStore *docStore,
                                   FullTextAssembler *fullText, SectionPicker *sectionPicker)
    : m_libraryStore(libraryStore)
    , m_docStore(docStore)
    , m_fullText(fullText)
    , m_sectionPicker(sectionPicker)
{
}

InjectionResult InjectionPlanner::plan(qint64 fileId, const QList<ChunkScore> &chunkScores,
                                       int effectivePerFile, int overlap,
                                       bool enableSections) const
{
    InjectionResult result;
    if (!m_libraryStore || !m_libraryStore->isAvailable()) {
        result.trace = QStringLiteral("数据库不可用");
        return result;
    }

    const QList<ChunkRecord> allChunks = m_libraryStore->chunksOfFile(fileId, nullptr);
    if (allChunks.isEmpty()) {
        result.trace = QStringLiteral("该文件尚无片段");
        return result;
    }
    for (const ChunkRecord &chunk : allChunks) {
        result.originalTokenEst += chunk.tokenEst;
    }
    if (effectivePerFile <= 0) {
        result.trace = QStringLiteral("预算不足");
        return result;
    }

    // ---- 1. 全文：整篇装得下就整篇注入（预算口径统一为入库时的 tokenEst）
    if (m_fullText) {
        const QString fullText = m_fullText->assemble(fileId, overlap, nullptr, nullptr);
        if (!fullText.isEmpty() && result.originalTokenEst <= effectivePerFile) {
            result.level = InjectionLevel::FullText;
            result.content = fullText;
            result.tokenEst = result.originalTokenEst;
            result.trace = QStringLiteral("全文注入（%1 tokens ≤ 预算 %2）")
                               .arg(result.originalTokenEst)
                               .arg(effectivePerFile);
            return result;
        }
    }

    // ---- 2. 章节：按章节内最高片段分排序，依次装入预算
    QString degradeReason = QStringLiteral("全文超出单文件预算 %1").arg(effectivePerFile);
    if (enableSections && m_sectionPicker && m_docStore) {
        DocumentRecord doc;
        if (m_docStore->documentByFile(fileId, &doc, nullptr)) {
            QString reason;
            const QList<SelectedSection> picked =
                m_sectionPicker->pick(fileId, chunkScores, effectivePerFile, overlap, &reason);

            if (!picked.isEmpty()) {
                // 恢复文件内自然顺序：按 sectionId 过滤全量章节
                QSet<qint64> pickedIds;
                for (const SelectedSection &section : picked) {
                    pickedIds.insert(section.sectionId);
                }
                const QList<SectionRecord> allSections =
                    m_docStore->sectionsOfDocument(doc.id, nullptr);
                QList<SectionRecord> ordered;
                for (const SectionRecord &section : allSections) {
                    if (pickedIds.contains(section.id)) {
                        ordered.append(section);
                    }
                }

                int usedTokens = 0;
                int skipped = 0;
                const QString content = buildSectionContent(fileId, ordered, overlap,
                                                            effectivePerFile, &usedTokens, &skipped);
                if (!content.isEmpty()) {
                    result.level = InjectionLevel::Section;
                    result.content = content;
                    result.tokenEst = usedTokens;
                    result.selectedSections = picked;
                    result.truncated = skipped > 0;
                    result.trace = QStringLiteral("%1 → 降级为章节注入（%2 个章节，%3 tokens）")
                                       .arg(degradeReason)
                                       .arg(picked.size())
                                       .arg(usedTokens);
                    if (skipped > 0) {
                        result.trace += QStringLiteral("，跳过 %1 个超预算章节").arg(skipped);
                    }
                    return result;
                }
            }
            if (!reason.isEmpty()) {
                degradeReason += QStringLiteral(" → %1").arg(reason);
            }
        }
    }

    // ---- 3. 片段回退
    int usedTokens = 0;
    const QString content =
        buildFragmentContent(allChunks, chunkScores, effectivePerFile, &usedTokens);
    result.level = InjectionLevel::Fragment;
    result.content = content;
    result.tokenEst = usedTokens;
    result.truncated = true;
    result.trace = QStringLiteral("%1 → 降级为片段注入（%2 tokens）")
                       .arg(degradeReason)
                       .arg(usedTokens);
    return result;
}

QString InjectionPlanner::buildSectionContent(qint64 fileId, const QList<SectionRecord> &sections,
                                              int overlap, int effectivePerFile, int *outTokens,
                                              int *outSkipped) const
{
    QStringList blocks;
    int usedTokens = 0;
    int skipped = 0;

    for (const SectionRecord &section : sections) {
        if (usedTokens + section.tokenEst > effectivePerFile) {
            ++skipped;
            continue;
        }
        const QString text = m_fullText->assembleRange(fileId, section.chunkStart, section.chunkEnd,
                                                       overlap, nullptr, nullptr);
        if (text.isEmpty()) {
            ++skipped;
            continue;
        }
        const QString title =
            section.title.isEmpty() ? QStringLiteral("未命名章节") : section.title;
        blocks.append(QStringLiteral("── %1 ──\n%2").arg(title, text));
        usedTokens += section.tokenEst;
    }

    if (outTokens) *outTokens = usedTokens;
    if (outSkipped) *outSkipped = skipped;
    return blocks.join(QStringLiteral("\n\n"));
}

QString InjectionPlanner::buildFragmentContent(const QList<ChunkRecord> &allChunks,
                                               const QList<ChunkScore> &chunkScores,
                                               int effectivePerFile, int *outTokens) const
{
    // ordinal → 片段，一次建索引，避免逐条线性查找
    QHash<int, ChunkRecord> byOrdinal;
    byOrdinal.reserve(allChunks.size());
    for (const ChunkRecord &chunk : allChunks) {
        byOrdinal.insert(chunk.ordinal, chunk);
    }

    QList<ChunkScore> sorted = chunkScores;
    std::sort(sorted.begin(), sorted.end(),
              [](const ChunkScore &a, const ChunkScore &b) { return a.score > b.score; });

    QStringList blocks;
    int usedTokens = 0;
    for (const ChunkScore &score : sorted) {
        const auto it = byOrdinal.constFind(score.ordinal);
        if (it == byOrdinal.constEnd()) {
            continue;
        }
        if (usedTokens + it->tokenEst > effectivePerFile) {
            continue;
        }
        blocks.append(it->text);
        usedTokens += it->tokenEst;
    }

    if (outTokens) *outTokens = usedTokens;
    return blocks.join(QStringLiteral("\n\n"));
}
