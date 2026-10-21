#include "core/retriever/FileLevelRetriever.h"

#include "core/retriever/BudgetCalculator.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryConfigStore.h"

#include <QHash>
#include <algorithm>

FileLevelRetriever::FileLevelRetriever(InjectionPlanner *planner, DocumentStore *docStore)
    : m_planner(planner)
    , m_docStore(docStore)
{
}

QList<DocumentInjectionResult> FileLevelRetriever::planInjection(
    const QList<RetrievedChunk> &chunks,
    const LibraryConfigData &config,
    int contextWindow,
    int historyTokens,
    QString *degradeNote) const
{
    QList<DocumentInjectionResult> results;
    if (chunks.isEmpty() || !m_planner) {
        return results;
    }

    // 上下文窗口钳制
    BudgetCalculator::Input input;
    input.modelContextWindow = contextWindow;
    input.historyTokenEstimate = historyTokens;
    input.userPerFileBudget = config.perFileBudget;
    input.userGlobalBudget = config.globalBudget;
    input.answerReserve = config.contextReserve;
    input.fallbackContextWindow = config.fallbackContextWindow;

    const BudgetCalculator::Output budget = BudgetCalculator::calculate(input);
    if (degradeNote) {
        *degradeNote = budget.degradeReason;
    }
    if (budget.effectiveGlobal <= 0) {
        return results;
    }

    const QList<FileScore> fileScores =
        aggregateByFile(chunks, config.fileAggregateWeight, config.similarityThreshold);

    int usedTokens = 0;
    for (const FileScore &fs : fileScores) {
        if (usedTokens >= budget.effectiveGlobal) {
            break;
        }
        const int perFileForThisFile =
            qMin(budget.effectivePerFile, budget.effectiveGlobal - usedTokens);

        // 章节结构是否存在（决定能否走章节级）
        QString sectionSource;
        bool hasSections = false;
        if (m_docStore && m_docStore->isAvailable()) {
            DocumentRecord doc;
            if (m_docStore->documentByFile(fs.fileId, &doc, nullptr)
                && !doc.sectionSource.isEmpty()
                && doc.sectionSource != QStringLiteral("none")) {
                hasSections = true;
                sectionSource = doc.sectionSource;
            }
        }

        const InjectionResult injection = m_planner->plan(
            fs.fileId, fs.chunkScores, perFileForThisFile, config.chunkOverlap, hasSections);
        if (injection.level == InjectionLevel::None) {
            continue;
        }

        usedTokens += injection.tokenEst;

        DocumentInjectionResult item;
        item.fileId = fs.fileId;
        item.collectionId = fs.collectionId;
        item.fileName = fs.fileName;
        item.fileScore = fs.aggregateScore;
        item.level = injection.level;
        item.content = injection.content;
        item.tokenEst = injection.tokenEst;
        item.truncated = injection.truncated;
        item.trace = injection.trace;
        item.sectionSource = sectionSource;
        results.append(item);
    }
    return results;
}

QList<FileLevelRetriever::FileScore> FileLevelRetriever::aggregateByFile(
    const QList<RetrievedChunk> &chunks, double weight, double threshold) const
{
    QHash<qint64, FileScore> byFile;
    for (const RetrievedChunk &chunk : chunks) {
        FileScore &fs = byFile[chunk.fileId];
        if (fs.hitCount == 0) {
            fs.fileId = chunk.fileId;
            fs.collectionId = chunk.collectionId;
            fs.fileName = chunk.fileName;
            fs.maxScore = chunk.score;
        } else if (chunk.score > fs.maxScore) {
            fs.maxScore = chunk.score;
        }
        ++fs.hitCount;
        fs.chunkScores.append({chunk.ordinal, chunk.score});
    }

    QList<FileScore> result;
    result.reserve(byFile.size());
    for (auto it = byFile.begin(); it != byFile.end(); ++it) {
        FileScore fs = it.value();
        fs.aggregateScore = fs.maxScore + fs.hitCount * weight;
        if (fs.aggregateScore >= threshold) {
            result.append(fs);
        }
    }

    std::sort(result.begin(), result.end(), [](const FileScore &a, const FileScore &b) {
        return a.aggregateScore > b.aggregateScore;
    });
    return result;
}
