#pragma once

#include "core/library/extract/Extractor.h"

/**
 * CSV / TSV 解析器：转成 **Markdown 表格**，保留行列语义
 * （如 `项目A | 预算 | 1200万`），并给出「第 12-18 行」的行号定位。
 *
 * 首行视为表头，每个片段重复表头以便单独阅读；每片段最多
 * `kTableRowsPerSegment` 条数据行，行号按**文件真实行号**（表头为第 1 行）标注。
 */
class CsvExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
