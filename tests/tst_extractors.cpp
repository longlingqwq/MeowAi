#include "core/library/extract/CsvExtractor.h"
#include "core/library/extract/DocxExtractor.h"
#include "core/library/extract/ExtractorRegistry.h"
#include "core/library/extract/JsonExtractor.h"
#include "core/library/extract/PdfTextExtractor.h"
#include "core/library/extract/PlainTextExtractor.h"
#include "core/library/extract/PptxExtractor.h"
#include "core/library/extract/XlsxExtractor.h"

#include <QDir>
#include <QFile>
#include <QPair>
#include <QTemporaryDir>
#include <QtTest>
#include <QtZlib/zlib.h>

#include <cstring>
#include <memory>

namespace {

QString writeFile(const QTemporaryDir &tmp, const QString &name, const QByteArray &bytes)
{
    const QString path = QDir(tmp.path()).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(bytes);
    file.close();
    return path;
}

/// 展平后的定位串集合，便于断言
QStringList locLabels(const ExtractionResult &result)
{
    QStringList labels;
    for (const ExtractedSegment &segment : result.segments) {
        labels.append(segment.locLabel);
    }
    return labels;
}

// ---------------------------------------------------- 测试用 ZIP 写入器（仅造夹具）
//
// Office 三件套本质是 ZIP+XML。为了**真实覆盖 deflate 解压路径**
// （真实 Office 文件都是 deflate 存储），这里用 Qt 自带 zlib 现场生成
// stored / deflated 两种合法 ZIP，而不用预置二进制夹具。

void putU16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void putU32(QByteArray &out, quint32 value)
{
    for (int i = 0; i < 4; ++i) {
        out.append(char((value >> (8 * i)) & 0xFF));
    }
}

QByteArray deflateRaw(const QByteArray &input)
{
    QByteArray output;
    output.resize(int(input.size()) + 128);

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = uInt(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = uInt(output.size());

    if (z_deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY)
        != Z_OK) {
        return QByteArray();
    }
    const int status = deflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END) {
        return QByteArray();
    }
    output.resize(int(produced));
    return output;
}

/// 写出一个合法 ZIP（`useDeflate` 决定压缩方式）；条目名按 UTF-8 编码
bool writeZip(const QString &path, const QList<QPair<QString, QByteArray>> &entries, bool useDeflate)
{
    struct Central
    {
        QByteArray name;
        quint16 method = 0;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localOffset = 0;
    };

    QByteArray out;
    QList<Central> centralDirectory;

    for (const QPair<QString, QByteArray> &entry : entries) {
        const QByteArray raw = entry.second;
        QByteArray stored = raw;
        quint16 method = 0;
        if (useDeflate && !raw.isEmpty()) {
            stored = deflateRaw(raw);
            if (stored.isEmpty()) {
                return false;
            }
            method = 8;
        }
        const quint32 crc = quint32(z_crc32(0L, reinterpret_cast<const Bytef *>(raw.constData()),
                                            uInt(raw.size())));

        Central central;
        central.name = entry.first.toUtf8();
        central.method = method;
        central.crc = crc;
        central.compressedSize = quint32(stored.size());
        central.uncompressedSize = quint32(raw.size());
        central.localOffset = quint32(out.size());
        centralDirectory.append(central);

        putU32(out, 0x04034b50u);  // 本地头签名
        putU16(out, 20);           // 解压所需版本
        putU16(out, 0);            // 通用标志
        putU16(out, method);
        putU16(out, 0);            // 修改时间
        putU16(out, 0x21);         // 修改日期（1980-01-01）
        putU32(out, crc);
        putU32(out, central.compressedSize);
        putU32(out, central.uncompressedSize);
        putU16(out, quint16(central.name.size()));
        putU16(out, 0);            // 额外字段长度
        out.append(central.name);
        out.append(stored);
    }

    const quint32 centralOffset = quint32(out.size());
    for (const Central &central : centralDirectory) {
        putU32(out, 0x02014b50u);  // 中央目录签名
        putU16(out, 20);           // 创建版本
        putU16(out, 20);           // 解压所需版本
        putU16(out, 0);            // 通用标志
        putU16(out, central.method);
        putU16(out, 0);
        putU16(out, 0x21);
        putU32(out, central.crc);
        putU32(out, central.compressedSize);
        putU32(out, central.uncompressedSize);
        putU16(out, quint16(central.name.size()));
        putU16(out, 0);            // 额外字段
        putU16(out, 0);            // 条目注释
        putU16(out, 0);            // 起始磁盘号
        putU16(out, 0);            // 内部属性
        putU32(out, 0);            // 外部属性
        putU32(out, central.localOffset);
        out.append(central.name);
    }
    const quint32 centralSize = quint32(out.size()) - centralOffset;

    putU32(out, 0x06054b50u);  // 中央目录结束记录
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, quint16(centralDirectory.size()));
    putU16(out, quint16(centralDirectory.size()));
    putU32(out, centralSize);
    putU32(out, centralOffset);
    putU16(out, 0);  // 注释长度

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const bool ok = file.write(out) == out.size();
    file.close();
    return ok;
}

// ------------------------------------------------------------- Office 夹具

QList<QPair<QString, QByteArray>> docxEntries()
{
    const QString documentXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>"
        "<w:p><w:r><w:t>工程概况</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>本工程共 12 栋楼。</w:t></w:r></w:p>"
        "<w:p/>"
        "<w:tbl>"
        "<w:tr><w:tc><w:p><w:r><w:t>项目</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:p><w:r><w:t>金额</w:t></w:r></w:p></w:tc></w:tr>"
        "<w:tr><w:tc><w:p><w:r><w:t>土建</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:p><w:r><w:t>1200万</w:t></w:r></w:p></w:tc></w:tr>"
        "</w:tbl>"
        "<w:p><w:r><w:t>附注：含税。</w:t></w:r></w:p>"
        "</w:body></w:document>");

    const QString contentTypes = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>");

    return {
        { QStringLiteral("[Content_Types].xml"), contentTypes.toUtf8() },
        { QStringLiteral("word/document.xml"), documentXml.toUtf8() },
    };
}

QList<QPair<QString, QByteArray>> xlsxEntries()
{
    const QString sheetXml = []( ) {
        QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<sheetData>"
            "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>");
        for (int i = 1; i <= 25; ++i) {
            const int rowNumber = i + 1;
            if (rowNumber == 24) {
                // 稀疏行：A 列留空，验证列对齐不被挤掉
                xml += QStringLiteral("<row r=\"24\"><c r=\"B24\"><v>240</v></c></row>");
                continue;
            }
            xml += QStringLiteral("<row r=\"%1\"><c r=\"A%1\" t=\"inlineStr\"><is><t>项目%2</t></is></c>"
                                  "<c r=\"B%1\"><v>%3</v></c></row>")
                       .arg(rowNumber)
                       .arg(i)
                       .arg(i * 10);
        }
        xml += QStringLiteral("</sheetData></worksheet>");
        return xml;
    }();

    const QString sharedStrings = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"2\" "
        "uniqueCount=\"2\"><si><t>名称</t></si><si><t>预算</t></si></sst>");

    const QString workbook = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"预算表\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");

    const QString relationships = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>");

    return {
        { QStringLiteral("xl/workbook.xml"), workbook.toUtf8() },
        { QStringLiteral("xl/_rels/workbook.xml.rels"), relationships.toUtf8() },
        { QStringLiteral("xl/sharedStrings.xml"), sharedStrings.toUtf8() },
        { QStringLiteral("xl/worksheets/sheet1.xml"), sheetXml.toUtf8() },
    };
}

QString slideXml(const QString &title, const QString &body)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
               "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
               "<p:cSld><p:spTree><p:sp><p:txBody>"
               "<a:p><a:r><a:t>%1</a:t></a:r></a:p>"
               "<a:p><a:r><a:t>%2</a:t></a:r></a:p>"
               "</p:txBody></p:sp></p:spTree></p:cSld></p:sld>")
        .arg(title, body);
}

QList<QPair<QString, QByteArray>> pptxEntries()
{
    return {
        { QStringLiteral("ppt/slides/slide1.xml"),
          slideXml(QStringLiteral("项目汇报"), QStringLiteral("本周完成 3 项")).toUtf8() },
        { QStringLiteral("ppt/slides/slide2.xml"),
          slideXml(QStringLiteral("第二页"), QStringLiteral("第二页要点")).toUtf8() },
        // 故意加入第 10 张：验证按数字排序而不是按字符串排序
        { QStringLiteral("ppt/slides/slide10.xml"),
          slideXml(QStringLiteral("第十页"), QStringLiteral("第十页要点")).toUtf8() },
    };
}

// ---------------------------------------------------- 测试用 PDF 写入器（仅造夹具）
//
// 手写最小合法 PDF（Catalog / Pages / Font / 每页一个内容流 + 页对象），
// 交叉引用表的偏移在生成时精确计算，保证 PDFium 能正常解析与抽取文本层。
// 文本用 Base14 的 Helvetica + Tj 书写，无需嵌入字体。
// 传入空字符串的页只放图形指令（无文本层）→ 模拟扫描件。

QByteArray pdfBytes(const QStringList &pageTexts)
{
    const int pageCount = pageTexts.size();

    QByteArray out = "%PDF-1.4\n";
    QList<int> offsets;  // offsets[对象号] = 对象起始偏移（下标与对象号对齐，1 起）
    offsets.append(0);

    const auto beginObject = [&out, &offsets](int number) {
        offsets.append(out.size());
        out += QByteArray::number(number) + " 0 obj\n";
    };

    QByteArray kids;
    for (int i = 0; i < pageCount; ++i) {
        kids += QByteArray::number(5 + 2 * i) + " 0 R ";  // 页对象号 = 5 + 2i
    }

    beginObject(1);
    out += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    beginObject(2);
    out += "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageCount)
        + " >>\nendobj\n";

    beginObject(3);
    out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n";

    for (int i = 0; i < pageCount; ++i) {
        // 第 i 页：内容流对象号 = 4 + 2i
        QByteArray content;
        if (pageTexts.at(i).isEmpty()) {
            content = "0 0 100 100 re f\n";  // 只有图形，无文本层
        } else {
            content = "BT /F1 24 Tf 72 720 Td (" + pageTexts.at(i).toLatin1() + ") Tj ET\n";
        }

        beginObject(4 + 2 * i);
        out += "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content
            + "endstream\nendobj\n";

        beginObject(5 + 2 * i);
        out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources "
               "<< /Font << /F1 3 0 R >> >> /Contents "
            + QByteArray::number(4 + 2 * i) + " 0 R >>\nendobj\n";
    }

    const int objectCount = offsets.size() - 1;
    const int xrefOffset = out.size();

    out += "xref\n0 " + QByteArray::number(objectCount + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (int number = 1; number <= objectCount; ++number) {
        out += QByteArray::number(offsets.at(number)).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objectCount + 1) + " /Root 1 0 R >>\n"
        + "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return out;
}

/// 同 id 的假解析器：验证「后注册覆盖先注册」
class StubJsonExtractor : public Extractor
{
public:
    QString id() const override { return QStringLiteral("json"); }

    bool supports(LibraryKind kind, const QString &extension) const override
    {
        return kind == LibraryKind::Text && extension == QStringLiteral("json");
    }

    bool extract(const QString &path, ExtractionResult *result, QString *error) const override
    {
        Q_UNUSED(path);
        if (result) {
            *result = ExtractionResult();
            result->ok = true;
        }
        if (error) {
            error->clear();
        }
        return true;
    }
};

}  // namespace

/**
 * 解析层（模块 M3-a）测试：
 *  - 注册表按类别 + 扩展名路由，未实现的格式必须给出明确错误（不留空指针崩溃）；
 *  - 每个片段**必须带定位信息**（段落序号 / 行号区间 / JSON 路径）；
 *  - CSV 转 Markdown 表格、引号内的分隔符与换行不破坏字段；
 *  - 编码与换行差异（BOM / CRLF / 非 UTF-8）不丢字、不崩。
 */
class TstExtractors : public QObject
{
    Q_OBJECT

private slots:
    void registryRoutesByExtension();
    void registryIsOverridableBySameId();
    void plainTextSplitsParagraphsWithLocators();
    void plainTextHandlesBomCrlfAndEmptyFile();
    void plainTextDoesNotDropNonUtf8Content();
    void csvConvertsToMarkdownTableWithRowRanges();
    void csvKeepsQuotedDelimiterAndNewlineInOneField();
    void jsonFlattensBranchesWithPathLabels();
    void jsonInvalidContentReportsParseError();
    void videoIsReservedAndReportsNotImplemented();
    void missingFileAndUnknownFormatFailWithMessage();
    void docxExtractsParagraphsAndTableRows();
    void xlsxExtractsSheetRowsWithLocators();
    void pptxExtractsSlidesInNumericOrder();
    void officeBrokenContainerReportsError();
    void pdfExtractsPageTextWithLocators();
    void pdfReportsMissingTextLayerSeparately();
};

void TstExtractors::registryRoutesByExtension()
{
    const ExtractorRegistry registry;

    const QStringList ids = registry.ids();
    QVERIFY(ids.contains(QStringLiteral("plain")));
    QVERIFY(ids.contains(QStringLiteral("csv")));
    QVERIFY(ids.contains(QStringLiteral("json")));
    QVERIFY(ids.contains(QStringLiteral("pdf")));
    QVERIFY(ids.contains(QStringLiteral("docx")));
    QVERIFY(ids.contains(QStringLiteral("xlsx")));
    QVERIFY(ids.contains(QStringLiteral("pptx")));
    QVERIFY(ids.contains(QStringLiteral("image")));
    QVERIFY(ids.contains(QStringLiteral("video")));

    QCOMPARE(registry.forPath(QStringLiteral("笔记.md"))->id(), QStringLiteral("plain"));
    QCOMPARE(registry.forPath(QStringLiteral("数据.csv"))->id(), QStringLiteral("csv"));
    QCOMPARE(registry.forPath(QStringLiteral("数据.tsv"))->id(), QStringLiteral("csv"));
    QCOMPARE(registry.forPath(QStringLiteral("配置.json"))->id(), QStringLiteral("json"));
    QCOMPARE(registry.forPath(QStringLiteral("片段.mp4"))->id(), QStringLiteral("video"));
    QCOMPARE(registry.forPath(QStringLiteral("报告.docx"))->id(), QStringLiteral("docx"));
    QCOMPARE(registry.forPath(QStringLiteral("表格.xlsx"))->id(), QStringLiteral("xlsx"));
    QCOMPARE(registry.forPath(QStringLiteral("演示.pptx"))->id(), QStringLiteral("pptx"));
    // PDF 由 PdfTextExtractor 负责：无 PDFium 时它仍登记，只在 extract() 时明确报「未启用」
    QCOMPARE(registry.forPath(QStringLiteral("报告.pdf"))->id(), QStringLiteral("pdf"));

    // 图片：经同一注册点接入，本地产出待外发的 caption 任务（描述由上层经 providers 层补齐）
    QCOMPARE(registry.forPath(QStringLiteral("照片.jpg"))->id(), QStringLiteral("image"));
    QCOMPARE(registry.forPath(QStringLiteral("照片.webp"))->id(), QStringLiteral("image"));
    QVERIFY(registry.forPath(QStringLiteral("未知.xyz")) == nullptr);
}

void TstExtractors::registryIsOverridableBySameId()
{
    ExtractorRegistry registry;
    registry.add(std::make_unique<StubJsonExtractor>());

    int jsonCount = 0;
    for (const QString &id : registry.ids()) {
        if (id == QStringLiteral("json")) {
            ++jsonCount;
        }
    }
    QCOMPARE(jsonCount, 1);  // 同 id 只保留最新

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeFile(tmp, QStringLiteral("配置.json"), R"({"a":1})");
    QVERIFY(!path.isEmpty());

    ExtractionResult result;
    QString error;
    QVERIFY(registry.extract(path, &result, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(result.ok);
    QVERIFY(result.segments.isEmpty());  // 已被假解析器接管，未走真实 JSON 展开
}

void TstExtractors::plainTextSplitsParagraphsWithLocators()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeFile(tmp, QStringLiteral("笔记.md"),
                                   QByteArray("第一段第一行\n第一段第二行\n\n第二段\n\n\n第三段\n"));
    QVERIFY(!path.isEmpty());

    const PlainTextExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);

    QCOMPARE(result.segments.size(), 3);
    QCOMPARE(locLabels(result),
             QStringList({ QStringLiteral("第 1 段"), QStringLiteral("第 2 段"), QStringLiteral("第 3 段") }));
    QCOMPARE(result.segments.at(0).text, QStringLiteral("第一段第一行\n第一段第二行"));
    QCOMPARE(result.segments.at(2).text, QStringLiteral("第三段"));
    for (const ExtractedSegment &segment : result.segments) {
        QCOMPARE(segment.locKind, LocatorKind::Para);
        QCOMPARE(locatorKindName(segment.locKind), QStringLiteral("para"));
    }
}

void TstExtractors::plainTextHandlesBomCrlfAndEmptyFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // UTF-8 BOM + CRLF：BOM 必须去掉、\r 必须归一
    const QString withBom = writeFile(tmp, QStringLiteral("bom.txt"),
                                      QByteArray("\xEF\xBB\xBF甲\r\n\r\n乙\r\n"));
    QVERIFY(!withBom.isEmpty());

    const PlainTextExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(withBom, &result, &error), qPrintable(error));
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(result.segments.at(0).text, QStringLiteral("甲"));
    QCOMPARE(result.segments.at(1).text, QStringLiteral("乙"));

    // 空文件：合法，入库但零片段，不报错
    const QString empty = writeFile(tmp, QStringLiteral("空.txt"), QByteArray());
    QVERIFY(!empty.isEmpty());
    ExtractionResult emptyResult;
    QVERIFY2(extractor.extract(empty, &emptyResult, &error), qPrintable(error));
    QVERIFY(emptyResult.ok);
    QVERIFY(emptyResult.segments.isEmpty());
}

void TstExtractors::plainTextDoesNotDropNonUtf8Content()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // 非法 UTF-8 序列（GBK 的「你好」）：应回落本地编码，而不是留下替换字符
    const QString path = writeFile(tmp, QStringLiteral("gbk.txt"), QByteArray("\xC4\xE3\xBA\xC3"));
    QVERIFY(!path.isEmpty());

    const PlainTextExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
    QCOMPARE(result.segments.size(), 1);
    QVERIFY(!result.segments.at(0).text.isEmpty());
    QVERIFY(!result.segments.at(0).text.contains(QChar::ReplacementCharacter));
}

void TstExtractors::csvConvertsToMarkdownTableWithRowRanges()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray csv = QByteArray("名称,预算,备注\n");
    for (int i = 1; i <= 25; ++i) {
        csv += QStringLiteral("项目%1,%2万,%3\n")
                   .arg(i)
                   .arg(i * 10)
                   .arg(i == 1 ? QStringLiteral("含|竖线") : QStringLiteral("普通"))
                   .toUtf8();
    }
    const QString path = writeFile(tmp, QStringLiteral("工程.csv"), csv);
    QVERIFY(!path.isEmpty());

    const CsvExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);

    // 表头占第 1 行，20 行一段 → 两段：第 2-21 行、第 22-26 行
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(locLabels(result), QStringList({ QStringLiteral("第 2-21 行"), QStringLiteral("第 22-26 行") }));

    const ExtractedSegment &first = result.segments.at(0);
    QCOMPARE(first.locKind, LocatorKind::Row);
    QCOMPARE(first.locRowFrom, 2);
    QCOMPARE(first.locRowTo, 21);
    QVERIFY(first.text.startsWith(QStringLiteral("| 名称 | 预算 | 备注 |")));
    QVERIFY(first.text.contains(QStringLiteral("| --- | --- | --- |")));
    QVERIFY(first.text.contains(QStringLiteral("| 项目1 | 10万 | 含\\|竖线 |")));  // 单元格内的 | 已转义

    const ExtractedSegment &second = result.segments.at(1);
    QCOMPARE(second.locRowFrom, 22);
    QCOMPARE(second.locRowTo, 26);
    QVERIFY(second.text.contains(QStringLiteral("| 项目25 | 250万 | 普通 |")));
    // 每段自带表头，单独阅读仍有列含义
    QVERIFY(second.text.startsWith(QStringLiteral("| 名称 | 预算 | 备注 |")));
}

void TstExtractors::csvKeepsQuotedDelimiterAndNewlineInOneField()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeFile(tmp, QStringLiteral("引号.csv"),
                                   QByteArray("甲,乙\n\"x,1\",\"y\n2\"\n"));
    QVERIFY(!path.isEmpty());

    const CsvExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));

    QCOMPARE(result.segments.size(), 1);
    const QString text = result.segments.at(0).text;
    QVERIFY(text.contains(QStringLiteral("| x,1 |")));       // 引号内的分隔符不拆列
    QVERIFY(text.contains(QStringLiteral("y<br>2")));        // 引号内的换行不拆行
    QCOMPARE(result.segments.at(0).locRowFrom, 2);
    QCOMPARE(result.segments.at(0).locRowTo, 2);
    QCOMPARE(result.segments.at(0).locLabel, QStringLiteral("第 2 行"));
}

void TstExtractors::jsonFlattensBranchesWithPathLabels()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeFile(tmp, QStringLiteral("配置.json"),
                                   QByteArray("{\"server\":{\"host\":\"h\",\"port\":8080},"
                                              "\"tags\":[\"a\",\"b\"],\"empty\":{}}"));
    QVERIFY(!path.isEmpty());

    const JsonExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);

    // 顶层分支各成一段；空对象不产生片段
    QCOMPARE(locLabels(result), QStringList({ QStringLiteral("$.server"), QStringLiteral("$.tags") }));
    for (const ExtractedSegment &segment : result.segments) {
        QCOMPARE(segment.locKind, LocatorKind::Json);
    }
    QCOMPARE(result.segments.at(0).text,
             QStringLiteral("$.server.host: h\n$.server.port: 8080"));
    QCOMPARE(result.segments.at(1).text,
             QStringLiteral("$.tags[0]: a\n$.tags[1]: b"));
}

void TstExtractors::jsonInvalidContentReportsParseError()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeFile(tmp, QStringLiteral("坏.json"), QByteArray("{ \"a\": }"));
    QVERIFY(!path.isEmpty());

    const JsonExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY(!extractor.extract(path, &result, &error));
    QVERIFY(error.contains(QStringLiteral("JSON 解析失败")));
    QVERIFY(!result.ok);
}

void TstExtractors::videoIsReservedAndReportsNotImplemented()
{
    const ExtractorRegistry registry;
    Extractor *extractor = registry.forPath(QStringLiteral("片子.mp4"));
    QVERIFY(extractor != nullptr);
    QCOMPARE(extractor->id(), QStringLiteral("video"));

    ExtractionResult result;
    QString error;
    QVERIFY(!extractor->extract(QStringLiteral("片子.mp4"), &result, &error));
    QVERIFY(error.contains(QStringLiteral("未实现")));
}

void TstExtractors::missingFileAndUnknownFormatFailWithMessage()
{
    const ExtractorRegistry registry;

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const PlainTextExtractor extractor;
    ExtractionResult result;
    QString error;
    const QString missing = QDir(tmp.path()).filePath(QStringLiteral("没有.txt"));
    QVERIFY(!extractor.extract(missing, &result, &error));
    QVERIFY(error.contains(QStringLiteral("不存在")));

    error.clear();
    QVERIFY(!registry.extract(QDir(tmp.path()).filePath(QStringLiteral("未知.xyz")), &result, &error));
    QVERIFY(error.contains(QStringLiteral("暂不支持")));
}

void TstExtractors::docxExtractsParagraphsAndTableRows()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // stored 与 deflate 两种存储都必须能解出（真实 Office 文件用的是 deflate）
    for (const bool deflated : { false, true }) {
        const QString path = QDir(tmp.path()).filePath(deflated ? QStringLiteral("deflated.docx")
                                                                : QStringLiteral("stored.docx"));
        QVERIFY(writeZip(path, docxEntries(), deflated));

        const DocxExtractor extractor;
        ExtractionResult result;
        QString error;
        QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
        QVERIFY(result.ok);

        QCOMPARE(result.segments.size(), 5);  // 空段落不产出片段
        QCOMPARE(locLabels(result),
                 QStringList({ QStringLiteral("第 1 段"), QStringLiteral("第 2 段"),
                               QStringLiteral("第 3 段"), QStringLiteral("第 4 段"),
                               QStringLiteral("第 5 段") }));
        QCOMPARE(result.segments.at(0).text, QStringLiteral("工程概况"));
        QCOMPARE(result.segments.at(1).text, QStringLiteral("本工程共 12 栋楼。"));
        QCOMPARE(result.segments.at(2).text, QStringLiteral("项目 | 金额"));  // 表格按行、单元格以 | 连接
        QCOMPARE(result.segments.at(3).text, QStringLiteral("土建 | 1200万"));
        QCOMPARE(result.segments.at(4).text, QStringLiteral("附注：含税。"));
        for (const ExtractedSegment &segment : result.segments) {
            QCOMPARE(segment.locKind, LocatorKind::Para);
        }
    }
}

void TstExtractors::xlsxExtractsSheetRowsWithLocators()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("预算.xlsx"));
    QVERIFY(writeZip(path, xlsxEntries(), true));

    const XlsxExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);

    // 表头为第 1 行（取自共享字符串），数据行 2..26 → 两段
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(locLabels(result), QStringList({ QStringLiteral("预算表 第 2-21 行"),
                                              QStringLiteral("预算表 第 22-26 行") }));

    const ExtractedSegment &first = result.segments.at(0);
    QCOMPARE(first.locKind, LocatorKind::Sheet);
    QCOMPARE(first.locSheet, QStringLiteral("预算表"));
    QCOMPARE(first.locRowFrom, 2);
    QCOMPARE(first.locRowTo, 21);
    QVERIFY(first.text.startsWith(QStringLiteral("| 名称 | 预算 |")));
    QVERIFY(first.text.contains(QStringLiteral("| 项目1 | 10 |")));

    const ExtractedSegment &second = result.segments.at(1);
    QCOMPARE(second.locRowFrom, 22);
    QCOMPARE(second.locRowTo, 26);
    QVERIFY(second.text.contains(QStringLiteral("| 项目25 | 250 |")));
    // 稀疏行（A24 留空）必须保留列对齐：中间列不塌陷
    QVERIFY(second.text.contains(QStringLiteral("|  | 240 |")));
}

void TstExtractors::pptxExtractsSlidesInNumericOrder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("汇报.pptx"));
    QVERIFY(writeZip(path, pptxEntries(), true));

    const PptxExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY2(extractor.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);

    QCOMPARE(result.segments.size(), 3);
    QCOMPARE(locLabels(result), QStringList({ QStringLiteral("第 1 张幻灯片"),
                                              QStringLiteral("第 2 张幻灯片"),
                                              QStringLiteral("第 3 张幻灯片") }));
    QCOMPARE(result.segments.at(0).text, QStringLiteral("项目汇报\n本周完成 3 项"));
    QCOMPARE(result.segments.at(1).text, QStringLiteral("第二页\n第二页要点"));
    // slide10.xml 必须排在 slide2.xml 之后：按数字排序而非字符串排序
    QCOMPARE(result.segments.at(2).text, QStringLiteral("第十页\n第十页要点"));

    for (int i = 0; i < result.segments.size(); ++i) {
        QCOMPARE(result.segments.at(i).locKind, LocatorKind::Slide);
        QCOMPARE(result.segments.at(i).locPage, i + 1);
    }
}

void TstExtractors::officeBrokenContainerReportsError()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // 完全不是 ZIP：必须给出明确错误，而不是崩溃或静默吞掉
    const QString notZip = writeFile(tmp, QStringLiteral("假的.docx"),
                                     QByteArray("这根本不是 ZIP 容器，只是一段普通文字"));
    QVERIFY(!notZip.isEmpty());
    const DocxExtractor docx;
    ExtractionResult result;
    QString error;
    QVERIFY(!docx.extract(notZip, &result, &error));
    QVERIFY(error.contains(QStringLiteral("不是有效的 Office 文件")));

    // 是合法 ZIP，但缺少 word/document.xml
    QList<QPair<QString, QByteArray>> minimal;
    minimal.append({ QStringLiteral("[Content_Types].xml"), QByteArray("<Types/>") });
    const QString noDocument = QDir(tmp.path()).filePath(QStringLiteral("缺正文.docx"));
    QVERIFY(writeZip(noDocument, minimal, true));
    error.clear();
    QVERIFY(!docx.extract(noDocument, &result, &error));
    QVERIFY(error.contains(QStringLiteral("缺少 word/document.xml")));

    // 是合法 ZIP，但没有任何幻灯片
    const QString noSlide = QDir(tmp.path()).filePath(QStringLiteral("空.pptx"));
    QVERIFY(writeZip(noSlide, minimal, false));
    const PptxExtractor pptx;
    error.clear();
    QVERIFY(!pptx.extract(noSlide, &result, &error));
    QVERIFY(error.contains(QStringLiteral("未找到幻灯片")));
}

void TstExtractors::pdfExtractsPageTextWithLocators()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeFile(tmp, QStringLiteral("锅炉房手册.pdf"),
                                   pdfBytes({ QStringLiteral("Boiler Room Notes"),
                                              QStringLiteral("Pump 2 checks weekly") }));
    QVERIFY(!path.isEmpty());

    const ExtractorRegistry registry;
    QCOMPARE(registry.forPath(path)->id(), QStringLiteral("pdf"));

    ExtractionResult result;
    QString error;
#ifdef MEOWAI_HAVE_PDFIUM
    QVERIFY2(registry.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);
    QCOMPARE(result.segments.size(), 2);  // 每页一个片段
    QCOMPARE(locLabels(result), QStringList({ QStringLiteral("第 1 页"), QStringLiteral("第 2 页") }));
    for (int i = 0; i < result.segments.size(); ++i) {
        QCOMPARE(result.segments.at(i).locKind, LocatorKind::Page);
        QCOMPARE(result.segments.at(i).locPage, i + 1);
    }
    QCOMPARE(result.segments.at(0).text, QStringLiteral("Boiler Room Notes"));
    QVERIFY(result.segments.at(1).text.contains(QStringLiteral("Pump 2 checks weekly")));
#else
    // 无 PDFium 时的降级口径：仍由 pdf 解析器负责，但明确提示「未启用」而非静默成功
    QVERIFY(!registry.extract(path, &result, &error));
    QVERIFY(error.contains(QStringLiteral("未启用")));
    QVERIFY(result.segments.isEmpty());
#endif
}

void TstExtractors::pdfReportsMissingTextLayerSeparately()
{
#ifndef MEOWAI_HAVE_PDFIUM
    QSKIP("需要 PDFium 才能区分「无文本层」与其它失败原因");
#else
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // 只有图形指令的页（模拟扫描件）：必须落到「无文本层」而不是「打开失败」
    const QString scanned = writeFile(tmp, QStringLiteral("扫描件.pdf"),
                                      pdfBytes({ QString(), QString() }));
    QVERIFY(!scanned.isEmpty());

    const PdfTextExtractor extractor;
    ExtractionResult result;
    QString error;
    QVERIFY(!extractor.extract(scanned, &result, &error));
    QVERIFY(error.contains(QStringLiteral("无文本层")));
    QVERIFY(result.segments.isEmpty());

    // 非 PDF 内容：必须是「打开失败」+ 具体原因，不能与「无文本层」混淆
    const QString broken = writeFile(tmp, QStringLiteral("假.pdf"),
                                     QByteArray("这根本不是 PDF 文件"));
    QVERIFY(!broken.isEmpty());
    error.clear();
    QVERIFY(!extractor.extract(broken, &result, &error));
    QVERIFY(error.contains(QStringLiteral("打开失败")));
    QVERIFY(!error.contains(QStringLiteral("无文本层")));
#endif
}

QTEST_MAIN(TstExtractors)
#include "tst_extractors.moc"
