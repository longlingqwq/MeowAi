#pragma once

#include "core/library/extract/Extractor.h"

/**
 * PDF（.pdf）解析器：抽取**文本层**，每页一个片段，定位为「第 N 页」
 * （`loc_kind = page`，同时写入 `loc_page`）。
 *
 * 依赖可选的 PDFium（BSD-3-Clause；**本地放置、仓库忽略**，见 scripts/fetch-pdfium.ps1）：
 *  - 构建时检测到 third_party/pdfium/ → 定义 `MEOWAI_HAVE_PDFIUM`，走真实解析；
 *  - 未检测到 → 编译降级实现，`extract()` 返回 false 并提示「PDF 支持未启用」，
 *    由上层落成 no_text_layer；其它格式解析完全不受影响。
 *
 * 「无文本层（扫描件/纯图片）」与「加密」「格式损坏」分别给出明确原因，不静默返回空结果。
 * 本解析器只读本地文件、不做网络请求、**不做 OCR**（OCR 不在本次范围）。
 */
class PdfTextExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
