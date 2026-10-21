#include "core/retriever/SectionPicker.h"

#include "storage/DocumentStore.h"

#include <QList>
#include <algorithm>

namespace {

/// 给定一个 section 和一组 chunk 分数，返回该 section 内最高的相似度分数
double sectionMaxScore(const SectionRecord &section, const QList<ChunkScore> &scores)
{
    double maxScore = 0.0;
    for (const ChunkScore &s : scores) {
        if (s.ordinal >= section.chunkStart && s.ordinal <= section.chunkEnd) {
            if (s.score > maxScore) {
                maxScore = s.score;
            }
        }
    }
    return maxScore;
}

} // namespace

SectionPicker::SectionPicker(DocumentStore *docStore)
    : m_docStore(docStore)
{
}

QList<SelectedSection> SectionPicker::pick(qint64 fileId,
                                           const QList<ChunkScore> &chunkScores,
                                           int budgetTokens,
                                           int overlap,
                                           QString *outReason) const
{
    QList<SelectedSection> result;

    auto setReason = [&](const QString &reason) {
        if (outReason) *outReason = reason;
    };

    if (!m_docStore || !m_docStore->isAvailable()) {
        setReason(QStringLiteral("数据库不可用"));
        return result;
    }

    // 查 documents 行
    DocumentRecord doc;
    if (!m_docStore->documentByFile(fileId, &doc, nullptr)) {
        setReason(QStringLiteral("该文件无章节结构（未建 documents 元数据）"));
        return result;
    }

    // 查 sections
    QList<SectionRecord> sections = m_docStore->sectionsOfDocument(doc.id, nullptr);
    if (sections.isEmpty()) {
        setReason(QStringLiteral("该文件无章节结构（sections 为空）"));
        return result;
    }

    // 构造 section + 其最高相似度分数
    QList<QPair<double, SectionRecord>> scoredSections;
    for (const SectionRecord &section : sections) {
        const double score = sectionMaxScore(section, chunkScores);
        if (score > 0.0) {  // 没有命中任何相关片段的章节跳过
            scoredSections.append({score, section});
        }
    }

    if (scoredSections.isEmpty()) {
        setReason(QStringLiteral("所有章节均无相关命中"));
        return result;
    }

    // 按相似度降序排列
    std::sort(scoredSections.begin(), scoredSections.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    // 依次装入直到预算用尽
    int usedTokens = 0;
    for (const auto &item : scoredSections) {
        const SectionRecord &section = item.second;
        if (usedTokens + section.tokenEst > budgetTokens) {
            // 预算不够跳过这章，继续看后面更小的章
            continue;
        }
        SelectedSection sel;
        sel.sectionId = section.id;
        sel.title = section.title;
        sel.chunkStart = section.chunkStart;
        sel.chunkEnd = section.chunkEnd;
        sel.tokenEst = section.tokenEst;
        usedTokens += section.tokenEst;
        result.append(sel);

        if (usedTokens >= budgetTokens) {
            break;
        }
    }

    if (result.isEmpty()) {
        setReason(QStringLiteral("预算不足以装入任何章节"));
    }

    return result;
}
