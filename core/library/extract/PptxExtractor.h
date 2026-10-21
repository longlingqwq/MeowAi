#pragma once

#include "core/library/extract/Extractor.h"

/**
 * PowerPoint（.pptx）解析器：每张幻灯片一个片段，文本按段落以换行连接，
 * 定位为「第 N 张幻灯片」（`loc_kind = slide`，并用 `loc_page` 记序号）。
 *
 * 幻灯片按**文件名中的数字**排序（`slide2.xml` 在 `slide10.xml` 之前），
 * 避免按字符串排序导致顺序错乱。
 */
class PptxExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
