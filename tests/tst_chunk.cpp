#include "core/library/extract/Extractor.h"
#include "core/retriever/ChunkSplitter.h"
#include "storage/Database.h"
#include "storage/LibraryStore.h"
#include "storage/SqliteVectorIndex.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

namespace {

ExtractedSegment paragraph(const QString &text, int ordinal)
{
    ExtractedSegment segment;
    segment.text = text;
    segment.locKind = LocatorKind::Para;
    segment.locLabel = QStringLiteral("第 %1 段").arg(ordinal);
    return segment;
}

/// 造一段 Markdown 表格片段（CSV / XLSX 解析层的口径）
ExtractedSegment tableSegment(LocatorKind kind, const QString &sheet, int rowFrom, int rowTo,
                              int dataRows)
{
    QStringList lines;
    lines.append(QStringLiteral("| 项目 | 预算 |"));
    lines.append(QStringLiteral("| --- | --- |"));
    for (int i = 0; i < dataRows; ++i) {
        lines.append(QStringLiteral("| 项目%1 | %2元 |").arg(i + 1).arg((i + 1) * 100));
    }

    ExtractedSegment segment;
    segment.text = lines.join(QLatin1Char('\n'));
    segment.locKind = kind;
    segment.locSheet = sheet;
    segment.locRowFrom = rowFrom;
    segment.locRowTo = rowTo;
    segment.locLabel = sheet.isEmpty()
                           ? QStringLiteral("第 %1-%2 行").arg(rowFrom).arg(rowTo)
                           : QStringLiteral("%1 第 %2-%3 行").arg(sheet).arg(rowFrom).arg(rowTo);
    return segment;
}

QString longText(int paragraphs, int charsEach)
{
    QStringList blocks;
    for (int i = 0; i < paragraphs; ++i) {
        // 每段用不同字符：重叠 / 不重叠的断言才有区分度
        blocks.append(QString(charsEach, QLatin1Char(static_cast<char>('a' + i % 26))));
    }
    return blocks.join(QStringLiteral("\n\n"));
}

}  // namespace

/**
 * M4 切分与向量测试：
 *  - 切分：尺寸 / 重叠可配置且**行为随之改变**、表格按行切并重复表头、
 *    定位元数据逐条保留、ordinal 连续、空片段跳过；
 *  - 向量：float32 小端往返、按「厂商 + 模型」隔离、维度不符整批拒绝、
 *    按文件清理；无数据库时安全失败。
 */
class TstChunk : public QObject
{
    Q_OBJECT

private slots:
    void estimateTokensCountsCjkAsOne();
    void splitKeepsChunkSizeAndOverlap();
    void splitKeepsLocatorsAndOrdinal();
    void tableChunksRepeatHeaderAndRecomputeRows();

    void vectorRoundTripAndModelIsolation();
    void removeByFileClearsVectors();

private:
    /// 建立可用 store + 一个已登记的文件记录，返回 fileId（0 表示失败）
    static qint64 prepareFile(Database &db, QTemporaryDir &tmp, QString *error)
    {
        if (!db.open(QDir(tmp.path()).filePath(QStringLiteral("metadata.db")), error)) {
            return 0;
        }
        if (!db.ensureSchema(error)) {
            return 0;
        }
        LibraryStore store(&db);
        LibraryFileRecord file;
        file.displayName = QStringLiteral("工程预算.xlsx");
        file.sourceKind = QStringLiteral("upload");
        file.sourcePath = QDir(tmp.path()).filePath(QStringLiteral("工程预算.xlsx"));
        file.ext = QStringLiteral("xlsx");
        file.kind = QStringLiteral("text");
        file.status = QStringLiteral("indexed");
        qint64 fileId = 0;
        if (!store.insertFile(file, &fileId, error)) {
            return 0;
        }
        return fileId;
    }
};

void TstChunk::estimateTokensCountsCjkAsOne()
{
    QCOMPARE(estimateTokens(QStringLiteral("物业工程管理")), 6);
    QCOMPARE(estimateTokens(QStringLiteral("abcdefgh")), 2);
    QCOMPARE(estimateTokens(QStringLiteral("")), 0);
    // 中英混排：中文 2 字 + 8 个 ASCII 字符（8/4 = 2）
    QCOMPARE(estimateTokens(QStringLiteral("预算 budget67")), 2 + 3);
}

void TstChunk::splitKeepsChunkSizeAndOverlap()
{
    ChunkSplitOptions options;
    options.chunkSize = 200;
    options.overlap = 40;
    const ChunkSplitter splitter(options);
    QCOMPARE(splitter.options().chunkSize, 200);
    QCOMPARE(splitter.options().overlap, 40);

    QList<ExtractedSegment> segments;
    segments.append(paragraph(longText(20, 100), 1));
    const QList<ChunkRecord> chunks = splitter.split(7, segments);

    QVERIFY(chunks.size() > 5);
    for (const ChunkRecord &chunk : chunks) {
        QCOMPARE(chunk.fileId, 7);
        QCOMPARE(chunk.locKind, QStringLiteral("para"));
        QVERIFY(chunk.charCount <= 200);
        QCOMPARE(chunk.charCount, chunk.text.size());
        QVERIFY(chunk.tokenEst > 0);
    }
    // ordinal 在文件内连续递增：邻居扩展依赖它
    for (int i = 0; i < chunks.size(); ++i) {
        QCOMPARE(chunks.at(i).ordinal, i);
    }
    // 重叠生效：后一块的开头能在前一块里找到（末尾 40 字符被带入下一块）
    for (int i = 1; i < chunks.size(); ++i) {
        const QString head = chunks.at(i).text.left(30);
        QVERIFY2(chunks.at(i - 1).text.contains(head), "相邻片段未体现重叠");
    }
}

void TstChunk::splitKeepsLocatorsAndOrdinal()
{
    ChunkSplitter splitter;
    QList<ExtractedSegment> segments;
    segments.append(paragraph(QStringLiteral("第一页内容"), 1));
    ExtractedSegment page;
    page.text = QStringLiteral("第二页内容");
    page.locKind = LocatorKind::Page;
    page.locPage = 2;
    page.locLabel = QStringLiteral("第 2 页");
    segments.append(page);

    const QList<ChunkRecord> chunks = splitter.split(3, segments);
    QCOMPARE(chunks.size(), 2);
    QCOMPARE(chunks.at(0).ordinal, 0);
    QCOMPARE(chunks.at(1).ordinal, 1);
    QCOMPARE(chunks.at(0).locKind, QStringLiteral("para"));
    QCOMPARE(chunks.at(0).locLabel, QStringLiteral("第 1 段"));
    QCOMPARE(chunks.at(1).locKind, QStringLiteral("page"));
    QCOMPARE(chunks.at(1).locPage, 2);
    QCOMPARE(chunks.at(1).locLabel, QStringLiteral("第 2 页"));
}

void TstChunk::tableChunksRepeatHeaderAndRecomputeRows()
{
    // 目标尺寸压到刚好放不下全部数据行，强制拆成多块
    ChunkSplitOptions options;
    options.chunkSize = 90;
    options.overlap = 0;
    const ChunkSplitter splitter(options);

    QList<ExtractedSegment> segments;
    segments.append(tableSegment(LocatorKind::Row, QString(), 2, 26, 25));
    const QList<ChunkRecord> chunks = splitter.split(9, segments);

    QVERIFY(chunks.size() >= 2);
    for (int i = 0; i < chunks.size(); ++i) {
        const ChunkRecord &chunk = chunks.at(i);
        QCOMPARE(chunk.ordinal, i);
        QCOMPARE(chunk.locKind, QStringLiteral("row"));
        // 每块都重复表头 + 分隔行，脱离上下文也能读懂列含义
        QVERIFY(chunk.text.startsWith(QStringLiteral("| 项目 | 预算 |")));
        QVERIFY(chunk.text.contains(QStringLiteral("| --- | --- |")));
        QVERIFY(chunk.locRowFrom >= 2);
        QVERIFY(chunk.locRowTo >= chunk.locRowFrom);
    }
    // 首块从第 2 行开始（表头占第 1 行），行区间连续推进到第 26 行
    QCOMPARE(chunks.first().locRowFrom, 2);
    QCOMPARE(chunks.last().locRowTo, 26);
    for (int i = 1; i < chunks.size(); ++i) {
        QCOMPARE(chunks.at(i).locRowFrom, chunks.at(i - 1).locRowTo + 1);
    }
}

void TstChunk::vectorRoundTripAndModelIsolation()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    const qint64 fileId = prepareFile(db, tmp, &error);
    QVERIFY2(fileId > 0, qPrintable(error));

    LibraryStore store(&db);
    QList<ChunkRecord> chunks;
    for (int i = 0; i < 3; ++i) {
        ChunkRecord chunk;
        chunk.ordinal = i;
        chunk.text = QStringLiteral("片段 %1").arg(i);
        chunks.append(chunk);
    }
    QVERIFY2(store.replaceChunks(fileId, chunks, &error), qPrintable(error));
    const QList<ChunkRecord> stored = store.chunksOfFile(fileId, &error);
    QCOMPARE(stored.size(), 3);

    SqliteVectorIndex index(&db);
    QVERIFY(index.isAvailable());

    QList<StoredVector> vectors;
    for (int i = 0; i < 3; ++i) {
        StoredVector vector;
        vector.chunkId = stored.at(i).id;
        vector.fileId = fileId;
        vector.values = QVector<float>{static_cast<float>(i), 0.5f, -0.25f};
        vectors.append(vector);
    }
    QVERIFY2(index.upsert(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"), vectors,
                          &error),
             qPrintable(error));
    QCOMPARE(index.count(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"), &error), 3);

    const QList<StoredVector> loaded =
        index.vectors(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"), &error);
    QCOMPARE(loaded.size(), 3);
    for (int i = 0; i < loaded.size(); ++i) {
        QCOMPARE(loaded.at(i).chunkId, stored.at(i).id);
        QCOMPARE(loaded.at(i).fileId, fileId);
        QCOMPARE(loaded.at(i).values.size(), 3);
        QCOMPARE(loaded.at(i).values.at(0), static_cast<float>(i));
    }

    // schema 规定一个片段只有一条向量（chunk_id 主键）：换模型写入同一片段会覆盖旧向量，
    // 因此重建索引时必须按文件清理旧向量（removeByFile），避免新旧模型混用
    QList<StoredVector> other;
    StoredVector single;
    single.chunkId = stored.at(0).id;
    single.values = QVector<float>{1.0f, 2.0f, 3.0f, 4.0f};
    other.append(single);
    QVERIFY(index.upsert(QStringLiteral("qwen"), QStringLiteral("other-model"), other, &error));
    QCOMPARE(index.count(QStringLiteral("qwen"), QStringLiteral("text-embedding-v4"), &error), 2);
    QCOMPARE(index.count(QStringLiteral("qwen"), QStringLiteral("other-model"), &error), 1);
    const QList<StoredVector> replaced = index.vectors(QStringLiteral("qwen"),
                                                       QStringLiteral("other-model"), &error);
    QCOMPARE(replaced.size(), 1);
    QCOMPARE(replaced.first().values.size(), 4);  // 维度随新模型更新
}

void TstChunk::removeByFileClearsVectors()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    const qint64 fileId = prepareFile(db, tmp, &error);
    QVERIFY2(fileId > 0, qPrintable(error));
    LibraryStore store(&db);
    QList<ChunkRecord> chunks;
    for (int i = 0; i < 2; ++i) {
        ChunkRecord chunk;
        chunk.ordinal = i;
        chunk.text = QStringLiteral("片段 %1").arg(i);
        chunks.append(chunk);
    }
    QVERIFY(store.replaceChunks(fileId, chunks, &error));
    const QList<ChunkRecord> stored = store.chunksOfFile(fileId, &error);

    SqliteVectorIndex index(&db);
    QList<StoredVector> vectors;
    for (const ChunkRecord &chunk : stored) {
        StoredVector vector;
        vector.chunkId = chunk.id;
        vector.values = QVector<float>{0.1f, 0.2f};
        vectors.append(vector);
    }
    QVERIFY(index.upsert(QStringLiteral("zhipu"), QStringLiteral("embedding-3"), vectors, &error));
    QCOMPARE(index.count(QStringLiteral("zhipu"), QStringLiteral("embedding-3"), &error), 2);

    QVERIFY(index.removeByFile(fileId, &error));
    QCOMPARE(index.count(QStringLiteral("zhipu"), QStringLiteral("embedding-3"), &error), 0);
    QVERIFY(index.vectors(QStringLiteral("zhipu"), QStringLiteral("embedding-3"), &error).isEmpty());
}

QTEST_GUILESS_MAIN(TstChunk)
#include "tst_chunk.moc"
