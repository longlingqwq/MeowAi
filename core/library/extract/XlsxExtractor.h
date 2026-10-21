#pragma once

#include "core/library/extract/Extractor.h"

/**
 * Excel（.xlsx）解析器：按**工作表**逐一抽取，转成 Markdown 表格，
 * 定位为「工作表名 + 行号区间」（`loc_kind = sheet`、`loc_sheet`、`loc_row_from/to`）。
 *
 * 取值来源覆盖：共享字符串（`t="s"`）、内联字符串（`t="inlineStr"`）、
 * 公式结果（`t="str"`）与数值（无 `t`）。单元格按**列字母**定位，
 * 因此中间空列不会被挤掉，行列含义得以保留。
 *
 * 每张工作表的首个非空行视为表头（与 CSV 口径一致）；每片段最多
 * `kTableRowsPerSegment` 条数据行。
 */
class XlsxExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
