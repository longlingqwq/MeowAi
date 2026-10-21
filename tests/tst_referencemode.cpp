#include "core/library/DocumentMeta.h"
#include "core/library/extract/ExtractorRegistry.h"
#include "core/retriever/BudgetCalculator.h"
#include "core/retriever/FileLevelRetriever.h"
#include "core/retriever/FullTextAssembler.h"
#include "core/retriever/InjectionPlanner.h"
#include "core/retriever/SectionPicker.h"
#include "storage/Database.h"
#include "storage/DocumentStore.h"
#include "storage/LibraryConfigStore.h"
#include "storage/LibraryStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 参考资料模式（v0.2.6）测试：
 *  - BudgetCalculator：窗口充足 / 窗口不足 / 窗口未知三场景的钳制与降级提示；
 *  - FullTextAssembler：相邻片段重叠去重；
 *  - 三级注入决策：全文 → 章节 → 片段；
 *  - 文件级聚合：同一文件多片段只产出一条注入结果。
 */
class TstReferenceMode : public QObject
{
    Q_OBJECT

private slots:
    void budgetClampsToContextWindow();
    void budgetUnknownWindowFallsBack();
    void budgetExhaustedByHistory();
    void fullTextAssemblyRemovesOverlap();
    void smallFileInjectsFullText();
    void largeFileFallsBackToFragments();
    void documentMetaEnablesSectionInjection();
    void chunksOfSameFileProduceSingleResult();
    void markdownHeadingsProduceSections();
    void markdownExtractorDetectsHeadingLevel();
    void fallbackWindowOverridesDefault();

private:
    static ChunkRecord makeChunk(int ordinal, const QString &text)
    {
        ChunkRecord chunk;
        chunk.ordinal = ordinal;
        chunk.text = text;
        chunk.locKind = QStringLiteral("page");
        chunk.locLabel = QStringLiteral("第 %1 页").arg(ordinal + 1);
        chunk.locPage = ordinal + 1;
        chunk.charCount = text.size();
        chunk.tokenEst = 100;
        return chunk;
    }

    static qint64 addFile(LibraryStore &store, const QString &name,
                          const QList<ChunkRecord> &chunks, qint64 collectionId)
    {
        LibraryFileRecord file;
        file.displayName = name;
        file.sourceKind = QStringLiteral("upload");
        file.sourcePath = QStringLiteral("/tmp/%1").arg(name);
        file.ext = QStringLiteral("txt");
        file.kind = QStringLiteral("text");
        file.status = QStringLiteral("indexed");

        qint64 fileId = 0;
        if (!store.insertFile(file, &fileId, nullptr) || fileId <= 0) {
            return 0;
        }
        if (collectionId > 0) {
            store.linkFileToCollection(fileId, collectionId, nullptr);
        }
        store.replaceChunks(fileId, chunks, nullptr);
        return fileId;
    }

    static bool prepare(Database &db, QTemporaryDir &tmp, QString *error)
    {
        if (!db.open(QDir(tmp.path()).filePath(QStringLiteral("metadata.db")), error)) {
            return false;
        }
        return db.ensureSchema(error);
    }
};

void TstReferenceMode::budgetClampsToContextWindow()
{
    BudgetCalculator::Input input;
    input.modelContextWindow = 10000;
    input.historyTokenEstimate = 3000;
    input.answerReserve = 2000;
    input.userPerFileBudget = 20000;
    input.userGlobalBudget = 100000;

    const BudgetCalculator::Output out = BudgetCalculator::calculate(input);
    QCOMPARE(out.effectiveGlobal, 5000);   // 10000 - 3000 - 2000
    QCOMPARE(out.effectivePerFile, 5000);
    QVERIFY(out.willDegrade);
    QVERIFY(!out.degradeReason.isEmpty());
}

void TstReferenceMode::budgetUnknownWindowFallsBack()
{
    BudgetCalculator::Input input;
    input.modelContextWindow = 0;   // 未知
    input.historyTokenEstimate = 0;
    input.userPerFileBudget = 20000;
    input.userGlobalBudget = 100000;

    const BudgetCalculator::Output out = BudgetCalculator::calculate(input);
    QVERIFY(out.contextWindowUnknown);
    QCOMPARE(out.effectiveGlobal,
             BudgetCalculator::kDefaultContextWindow - input.answerReserve);
    QVERIFY(out.degradeReason.contains(QStringLiteral("未知")));
}

void TstReferenceMode::budgetExhaustedByHistory()
{
    BudgetCalculator::Input input;
    input.modelContextWindow = 8000;
    input.historyTokenEstimate = 7000;
    input.answerReserve = 2000;

    const BudgetCalculator::Output out = BudgetCalculator::calculate(input);
    QCOMPARE(out.effectiveGlobal, 0);
    QCOMPARE(out.effectivePerFile, 0);
    QVERIFY(out.willDegrade);
}

void TstReferenceMode::fullTextAssemblyRemovesOverlap()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY(prepare(db, tmp, &error));

    LibraryStore store(&db);
    qint64 collectionId = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库"), QString(), &collectionId, &error));

    // 第二段开头与第一段末尾重叠 4 个字符
    const QList<ChunkRecord> chunks = {
        makeChunk(0, QStringLiteral("AAAABBBB")),
        makeChunk(1, QStringLiteral("BBBBCCCC")),
    };
    const qint64 fileId = addFile(store, QStringLiteral("a.txt"), chunks, collectionId);
    QVERIFY(fileId > 0);

    FullTextAssembler assembler(&store);
    const QString text = assembler.assemble(fileId, 10, nullptr, &error);
    QCOMPARE(text, QStringLiteral("AAAABBBBCCCC"));
}

void TstReferenceMode::smallFileInjectsFullText()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY(prepare(db, tmp, &error));

    LibraryStore store(&db);
    DocumentStore docs(&db);
    FullTextAssembler assembler(&store);
    SectionPicker picker(&docs);
    InjectionPlanner planner(&store, &docs, &assembler, &picker);

    qint64 collectionId = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库"), QString(), &collectionId, &error));
    const qint64 fileId = addFile(store, QStringLiteral("small.txt"),
                                  {makeChunk(0, QStringLiteral("短文本"))}, collectionId);
    QVERIFY(fileId > 0);

    const InjectionResult result = planner.plan(fileId, {{0, 0.9}}, 20000, 0, false);
    QCOMPARE(result.level, InjectionLevel::FullText);
    QCOMPARE(result.content, QStringLiteral("短文本"));
}

void TstReferenceMode::largeFileFallsBackToFragments()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY(prepare(db, tmp, &error));

    LibraryStore store(&db);
    DocumentStore docs(&db);
    FullTextAssembler assembler(&store);
    SectionPicker picker(&docs);
    InjectionPlanner planner(&store, &docs, &assembler, &picker);

    qint64 collectionId = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库"), QString(), &collectionId, &error));
    const qint64 fileId = addFile(store, QStringLiteral("big.txt"),
                                  {makeChunk(0, QStringLiteral("段一")),
                                   makeChunk(1, QStringLiteral("段二")),
                                   makeChunk(2, QStringLiteral("段三"))},
                                  collectionId);
    QVERIFY(fileId > 0);

    // 预算 150 < 3 段 × 100：只能装入 1 段
    const InjectionResult result = planner.plan(fileId, {{2, 0.9}, {0, 0.5}}, 150, 0, false);
    QCOMPARE(result.level, InjectionLevel::Fragment);
    QCOMPARE(result.content, QStringLiteral("段三"));
    QVERIFY(result.truncated);
}

void TstReferenceMode::documentMetaEnablesSectionInjection()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY(prepare(db, tmp, &error));

    LibraryStore store(&db);
    DocumentStore docs(&db);
    FullTextAssembler assembler(&store);
    SectionPicker picker(&docs);
    InjectionPlanner planner(&store, &docs, &assembler, &picker);

    qint64 collectionId = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库"), QString(), &collectionId, &error));
    const qint64 fileId = addFile(store, QStringLiteral("manual.txt"),
                                  {makeChunk(0, QStringLiteral("施工规范")),
                                   makeChunk(1, QStringLiteral("验收标准"))},
                                  collectionId);
    QVERIFY(fileId > 0);

    DocumentRecord doc;
    doc.fileId = fileId;
    doc.tokenEstTotal = 200;
    doc.sectionSource = QStringLiteral("page_approx");
    doc.sectionCount = 2;
    qint64 docId = 0;
    QVERIFY(docs.upsertDocument(doc, &docId, &error));

    SectionRecord s0;
    s0.ordinal = 0;
    s0.title = QStringLiteral("施工规范");
    s0.chunkStart = 0;
    s0.chunkEnd = 0;
    s0.tokenEst = 100;
    SectionRecord s1;
    s1.ordinal = 1;
    s1.title = QStringLiteral("验收标准");
    s1.chunkStart = 1;
    s1.chunkEnd = 1;
    s1.tokenEst = 100;
    QVERIFY(docs.replaceSections(docId, {s0, s1}, &error));

    // 预算 120：全文 200 装不下 → 只能装入一个章节
    const InjectionResult result =
        planner.plan(fileId, {{0, 0.4}, {1, 0.95}}, 120, 0, true);
    QCOMPARE(result.level, InjectionLevel::Section);
    QVERIFY(result.content.contains(QStringLiteral("验收标准")));
    QVERIFY(!result.content.contains(QStringLiteral("施工规范")));
}

void TstReferenceMode::chunksOfSameFileProduceSingleResult()
{
    QTemporaryDir tmp;
    Database db;
    QString error;
    QVERIFY(prepare(db, tmp, &error));

    LibraryStore store(&db);
    DocumentStore docs(&db);
    FullTextAssembler assembler(&store);
    SectionPicker picker(&docs);
    InjectionPlanner planner(&store, &docs, &assembler, &picker);

    qint64 collectionId = 0;
    QVERIFY(store.insertCollection(QStringLiteral("库"), QString(), &collectionId, &error));
    const qint64 fileId = addFile(store, QStringLiteral("one.txt"),
                                  {makeChunk(0, QStringLiteral("正文"))}, collectionId);
    QVERIFY(fileId > 0);

    QList<RetrievedChunk> chunks;
    for (int i = 0; i < 3; ++i) {
        RetrievedChunk chunk;
        chunk.fileId = fileId;
        chunk.collectionId = collectionId;
        chunk.fileName = QStringLiteral("one.txt");
        chunk.ordinal = 0;
        chunk.score = 0.8;
        chunks.append(chunk);
    }

    LibraryConfigData config;
    config.similarityThreshold = 0.3;
    config.fileAggregateWeight = 0.05;

    FileLevelRetriever retriever(&planner, &docs);
    const QList<DocumentInjectionResult> results =
        retriever.planInjection(chunks, config, 128000, 0, nullptr);

    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().fileId, fileId);
    QVERIFY(results.first().fileScore > 0.8);   // 聚合分含命中数加权
}

void TstReferenceMode::markdownHeadingsProduceSections()
{
    QList<ChunkRecord> chunks;
    for (int i = 0; i < 5; ++i) {
        chunks.append(makeChunk(i, QStringLiteral("正文")));
    }

    QList<HeadingSpan> headings;
    headings.append(HeadingSpan{0, 1, QStringLiteral("总则")});
    headings.append(HeadingSpan{2, 2, QStringLiteral("验收")});

    const DocumentMeta meta = detectDocumentMeta(chunks, headings);
    QCOMPARE(meta.sectionSource, QStringLiteral("heading"));
    QCOMPARE(meta.sections.size(), 2);
    QCOMPARE(meta.sections.at(0).title, QStringLiteral("总则"));
    QCOMPARE(meta.sections.at(0).chunkStart, 0);
    QCOMPARE(meta.sections.at(0).chunkEnd, 1);
    QCOMPARE(meta.sections.at(1).title, QStringLiteral("验收"));
    QCOMPARE(meta.sections.at(1).chunkStart, 2);
    QCOMPARE(meta.sections.at(1).chunkEnd, 4);
}

void TstReferenceMode::markdownExtractorDetectsHeadingLevel()
{
    QTemporaryDir tmp;
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("manual.md"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QStringLiteral("# 总则\n\n正文一\n\n## 验收\n\n正文二\n").toUtf8());
    file.close();

    ExtractorRegistry registry;
    ExtractionResult result;
    QString error;
    QVERIFY(registry.extract(path, &result, &error));

    // 两条标题 + 两段正文
    QCOMPARE(result.segments.size(), 4);
    QCOMPARE(result.segments.at(0).headingLevel, 1);
    QCOMPARE(result.segments.at(0).text, QStringLiteral("总则"));
    QCOMPARE(result.segments.at(1).headingLevel, 0);
    QCOMPARE(result.segments.at(2).headingLevel, 2);
    QCOMPARE(result.segments.at(2).text, QStringLiteral("验收"));
    QCOMPARE(result.segments.at(3).headingLevel, 0);
}

void TstReferenceMode::fallbackWindowOverridesDefault()
{
    BudgetCalculator::Input input;
    input.modelContextWindow = 0;          // 厂商未提供
    input.fallbackContextWindow = 1000000; // 用户按所用 1M 模型设定
    input.userPerFileBudget = 20000;
    input.userGlobalBudget = 100000;
    input.answerReserve = 2000;

    const BudgetCalculator::Output out = BudgetCalculator::calculate(input);
    QVERIFY(out.contextWindowUnknown);
    // 1M 窗口充足 → 不再因窗口而降级
    QCOMPARE(out.effectiveGlobal, 100000);
    QCOMPARE(out.effectivePerFile, 20000);
}

QTEST_GUILESS_MAIN(TstReferenceMode)
#include "tst_referencemode.moc"
