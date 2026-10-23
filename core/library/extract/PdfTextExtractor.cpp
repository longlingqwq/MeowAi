#include "core/library/extract/PdfTextExtractor.h"

#ifdef MEOWAI_HAVE_PDFIUM
#include <fpdf_text.h>
#include <fpdfview.h>
#endif

#include <QByteArray>
#include <QFile>
#include <QVector>

namespace {

#ifdef MEOWAI_HAVE_PDFIUM

/// PDFium 全局库：进程内只初始化一次，静态析构时释放（PDFium 要求在退出前 DestroyLibrary）
struct PdfiumLibrary
{
    PdfiumLibrary() { FPDF_InitLibrary(); }
    ~PdfiumLibrary() { FPDF_DestroyLibrary(); }
};

void ensurePdfiumReady()
{
    static PdfiumLibrary library;
    Q_UNUSED(library);
}

/// FPDF_GetLastError() 错误码 → 人可读原因（对照 fpdfview.h 的 FPDF_ERR_* 取值）
QString pdfiumErrorMessage(unsigned long code)
{
    switch (code) {
    case FPDF_ERR_SUCCESS:
        return QStringLiteral("成功");
    case FPDF_ERR_FILE:
        return QStringLiteral("文件无法读取或已损坏");
    case FPDF_ERR_FORMAT:
        return QStringLiteral("不是有效的 PDF 格式");
    case FPDF_ERR_PASSWORD:
        return QStringLiteral("PDF 已加密，需要口令");
    case FPDF_ERR_SECURITY:
        return QStringLiteral("PDF 使用了不支持的加密方案");
    case FPDF_ERR_PAGE:
        return QStringLiteral("页面内容无法解析");
    case FPDF_ERR_UNKNOWN:
        break;
    }
    return QStringLiteral("未知错误");
}

/// 取单页文本层：PDFium 以 UTF-16 返回（换行是 \r\n），此处归一为 \n
QString pageTextLayer(FPDF_TEXTPAGE textPage)
{
    const int charCount = FPDFText_CountChars(textPage);
    if (charCount <= 0) {
        return QString();
    }
    // 契约：缓冲区至少 count + 1（含终止 NUL），返回值也含该 NUL
    QVector<unsigned short> buffer(charCount + 1);
    const int written = FPDFText_GetText(textPage, 0, charCount, buffer.data());
    if (written <= 0) {
        return QString();
    }
    QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()),
                                      written - 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

/// 逐行 trim 并丢弃空行：文本层常含整行空白，压缩后更利于后续切分与引用展示
QString compactLines(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList kept;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            kept.append(trimmed);
        }
    }
    return kept.join(QLatin1Char('\n'));
}

#endif  // MEOWAI_HAVE_PDFIUM

}  // namespace

QString PdfTextExtractor::id() const
{
    return QStringLiteral("pdf");
}

bool PdfTextExtractor::supports(LibraryKind kind, const QString &extension) const
{
    return kind == LibraryKind::Text && extension == QStringLiteral("pdf");
}

bool PdfTextExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }
    if (error) {
        error->clear();
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error) {
            *error = QStringLiteral("文件不存在或不是普通文件：%1").arg(path);
        }
        return false;
    }
    if (!validateExtractSize(info.size(), error)) {
        return false;
    }

#ifndef MEOWAI_HAVE_PDFIUM
    if (error) {
        *error = QStringLiteral("PDF 支持未启用：构建时未检测到 PDFium 运行库"
                                "（运行 pwsh scripts/fetch-pdfium.ps1 后重新配置构建即可启用文本层解析）");
    }
    return false;
#else
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法读取文件：%1").arg(path);
        }
        return false;
    }
    // 走内存加载而非按路径加载：避免含中文/非 ASCII 的路径在不同平台被当作本地编码解析
    const QByteArray bytes = file.readAll();
    file.close();

    ensurePdfiumReady();
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument64(bytes.constData(), static_cast<size_t>(bytes.size()), nullptr);
    if (!document) {
        if (error) {
            *error = QStringLiteral("PDF 打开失败：%1").arg(pdfiumErrorMessage(FPDF_GetLastError()));
        }
        return false;
    }

    const int pageCount = FPDF_GetPageCount(document);
    if (pageCount <= 0) {
        FPDF_CloseDocument(document);
        if (error) {
            *error = QStringLiteral("该 PDF 不包含任何页面");
        }
        return false;
    }

    QList<ExtractedSegment> segments;
    int failedPages = 0;
    int emptyPages = 0;

    for (int index = 0; index < pageCount; ++index) {
        FPDF_PAGE page = FPDF_LoadPage(document, index);
        if (!page) {
            ++failedPages;
            continue;
        }
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (!textPage) {
            FPDF_ClosePage(page);
            ++failedPages;
            continue;
        }
        const QString text = compactLines(pageTextLayer(textPage));
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);

        if (text.isEmpty()) {
            ++emptyPages;
            continue;
        }

        ExtractedSegment segment;
        segment.text = text;
        segment.locKind = LocatorKind::Page;
        segment.locPage = index + 1;
        segment.locLabel = QStringLiteral("第 %1 页").arg(index + 1);
        segments.append(segment);
    }
    FPDF_CloseDocument(document);

    if (segments.isEmpty()) {
        if (error) {
            if (emptyPages > 0) {
                *error = QStringLiteral("该 PDF 无文本层：%1 页均未解析出文字（可能为扫描件或纯图片 PDF，需 OCR）")
                             .arg(emptyPages);
            } else {
                *error = QStringLiteral("该 PDF 共 %1 页，页面全部解析失败（文件可能已损坏）").arg(pageCount);
            }
        }
        return false;
    }

    if (result) {
        result->ok = true;
        result->segments = segments;
    }
    if (error) {
        error->clear();
    }
    return true;
#endif
}
