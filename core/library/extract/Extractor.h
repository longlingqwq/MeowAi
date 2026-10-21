#pragma once

#include "core/library/LibraryFileKinds.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/**
 * 资料库解析层（模块 M3）的公共契约。
 *
 * 分层约束：本层只做**本地同步解析**，绝不发起网络请求。
 * 图片类不走本地 OCR：由 `ImageCaptionExtractor` 在本地完成**校验 + 压缩转码**，
 * 产出待外发的 `ImageCaptionTask`（见下），真正的视觉模型调用由上层索引器经
 * providers 层发起（需用户显式同意，见 docs/PROMPT.md §8）。
 * 所有失败一律 `return false` + 写 `error`，不得抛出异常。
 */

/// 片段定位种类；落库字符串与 schema v2 `chunks.loc_kind` 的取值一一对应
enum class LocatorKind
{
    None,   ///< 无定位信息（不应出现）
    Para,   ///< 段落：第 N 段
    Row,    ///< 表格行区间：第 12-18 行
    Sheet,  ///< 工作表
    Page,   ///< 页码
    Slide,  ///< 幻灯片
    Json,   ///< JSON 路径
    Image,  ///< 图片序号
};

/// `chunks.loc_kind` 的落库取值；`LocatorKind::None` 返回空串
inline QString locatorKindName(LocatorKind kind)
{
    switch (kind) {
    case LocatorKind::Para:
        return QStringLiteral("para");
    case LocatorKind::Row:
        return QStringLiteral("row");
    case LocatorKind::Sheet:
        return QStringLiteral("sheet");
    case LocatorKind::Page:
        return QStringLiteral("page");
    case LocatorKind::Slide:
        return QStringLiteral("slide");
    case LocatorKind::Json:
        return QStringLiteral("json");
    case LocatorKind::Image:
        return QStringLiteral("image");
    case LocatorKind::None:
        break;
    }
    return QString();
}

/// 单个片段：**必须携带定位信息**，否则引用来源无法向用户核对
struct ExtractedSegment
{
    QString text;                       ///< 片段文本（结构化抽取结果，如 Markdown 表格）
    LocatorKind locKind = LocatorKind::None;
    QString locLabel;                   ///< 人可读定位串：「第 3 页」「Sheet1 第 12-18 行」
    int locPage = 0;                    ///< 页码（1 起；不适用为 0）
    QString locSheet;                   ///< 工作表名（不适用为空）
    int locRowFrom = 0;                 ///< 行区间起（1 起、含；不适用为 0）
    int locRowTo = 0;                   ///< 行区间止（含；不适用为 0）
};

/**
 * 待外发的图片 caption 任务（图片类的本地部分产出）。
 *
 * **只承载「已本地转码、可安全外发」的字节**：外发的是转码后副本而非原图，
 * 且是否真的发送由上层在**用户显式同意**后决定（本层不发送、不落盘、不记日志）。
 */
struct ImageCaptionTask
{
    QByteArray bytes;        ///< 压缩后的图片字节（PNG/JPEG）
    QString mimeType;        ///< 如 "image/png"
    int width = 0;           ///< 压缩后宽
    int height = 0;          ///< 压缩后高
    QString sourcePath;      ///< 本地源文件路径（仅供上层定位，不外发）

    bool isValid() const { return !bytes.isEmpty() && !mimeType.isEmpty(); }
};

/// 一次抽取的结果
struct ExtractionResult
{
    bool ok = false;
    QList<ExtractedSegment> segments;
    /// 图片类专用：本地已转码、待上层经 providers 层生成描述的任务（文本类为空）
    QList<ImageCaptionTask> captionTasks;
};

/// 解析器接口（注册式：新增格式＝新增一个实现 + 在注册表登记，上层不写 if-else）
class Extractor
{
public:
    virtual ~Extractor() = default;

    /// 稳定标识（注册表与日志用）："plain" / "csv" / "json" / "video" …
    virtual QString id() const = 0;

    /// 是否由本解析器负责：按类别 + 扩展名（已小写、不含点）判定
    virtual bool supports(LibraryKind kind, const QString &extension) const = 0;

    /// 本地同步抽取；成功时置 `result->ok = true`。
    /// 失败（文件不存在 / 过大 / 解析失败）返回 false 并写 `error`，绝不抛异常
    virtual bool extract(const QString &path, ExtractionResult *result, QString *error) const = 0;
};

/**
 * 图片类的登记方式与文本类一致（同样经 `ExtractorRegistry` 路由），但产出口径不同：
 * 本地只做「校验 + 压缩转码」，结果放进 `ExtractionResult::captionTasks`、`segments` 为空；
 * 待上层在用户同意后经 providers 层拿到描述文本，再回填为「图片 #N」片段。
 * 这样同步接口不会偷偷联网，图片也仍然「经同一注册点接入」。
 */

/// 单次解析的字节上限：超过直接拒绝，避免一个超大文件把内存吃满
constexpr qint64 kMaxExtractBytes = 64LL * 1024 * 1024;

/// 表格类解析（CSV / XLSX）每片段容纳的数据行数（不含表头）：
/// 既保证「第 12-18 行」这类定位可核对，也避免片段过碎
constexpr int kTableRowsPerSegment = 20;

/// Markdown 单元格：`|` 转义为 `\|`、换行转 `<br>`，避免破坏表格结构
inline QString markdownCell(const QString &value)
{
    QString cell = value.trimmed();
    cell.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    cell.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return cell;
}

/// 按 `columns` 补齐空单元格后拼成 Markdown 行，保证整表列数一致
inline QString markdownRow(const QStringList &cells, int columns)
{
    QString line = QStringLiteral("|");
    for (int i = 0; i < columns; ++i) {
        line += QStringLiteral(" %1 |").arg(markdownCell(i < cells.size() ? cells.at(i) : QString()));
    }
    return line;
}

/// Markdown 表头分隔行
inline QString markdownSeparator(int columns)
{
    QString line = QStringLiteral("|");
    for (int i = 0; i < columns; ++i) {
        line += QStringLiteral(" --- |");
    }
    return line;
}

/**
 * 读取文本类文件为统一口径的字符串（纯文本 / CSV / JSON 三个解析器共用）：
 *  - 校验存在、是普通文件、未超 `kMaxExtractBytes`；
 *  - 去掉 UTF-8 BOM；
 *  - 先按 UTF-8 解码；出现替换字符（非法序列）时回落到本地编码（中文 Windows 即 GBK）；
 *  - `\r\n` 与孤立 `\r` 一律归一为 `\n`，避免后续分段/切分被换行符差异干扰。
 *
 * 失败返回 false 并写 `error`（含具体原因），绝不抛异常。
 */
inline bool readLibraryTextFile(const QString &path, QString *text, QString *error)
{
    if (text) {
        text->clear();
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error) {
            *error = QStringLiteral("文件不存在或不是普通文件：%1").arg(path);
        }
        return false;
    }
    if (info.size() > kMaxExtractBytes) {
        if (error) {
            *error = QStringLiteral("文件过大（%1 MB），超出单次解析上限 %2 MB")
                         .arg(info.size() / (1024 * 1024))
                         .arg(kMaxExtractBytes / (1024 * 1024));
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法读取文件：%1").arg(path);
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QByteArray payload = bytes;
    if (payload.startsWith("\xEF\xBB\xBF")) {
        payload.remove(0, 3);
    }

    QString decoded = QString::fromUtf8(payload);
    if (decoded.contains(QChar::ReplacementCharacter)) {
        decoded = QString::fromLocal8Bit(payload);
    }
    decoded.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    decoded.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    if (text) {
        *text = decoded;
    }
    if (error) {
        error->clear();
    }
    return true;
}
