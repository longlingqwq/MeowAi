#pragma once

#include "core/library/extract/Extractor.h"

/**
 * 纯文本解析器：`txt / md / markdown / log`。
 *
 * 按**空行分段**（Markdown 的自然块边界），每段一个片段并给出「第 N 段」定位；
 * 长度切分与重叠交给 M4 的 `ChunkSplitter`，本层只负责「语义块 + 定位」。
 */
class PlainTextExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
