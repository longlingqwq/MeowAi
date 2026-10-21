#pragma once

#include "core/library/extract/Extractor.h"

/**
 * Word（.docx）解析器：解 `word/document.xml`，按**段落**产出片段
 * （定位串「第 N 段」）；表格按**行**产出片段，单元格以 ` | ` 连接，
 * 借此保留表格的行列含义。
 *
 * 只取 `<w:t>` 内的文本：域代码（`w:instrText`）与修订删除文本（`w:delText`）不会混入。
 */
class DocxExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
