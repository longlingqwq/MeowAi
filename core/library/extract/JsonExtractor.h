#pragma once

#include "core/library/extract/Extractor.h"

/**
 * JSON 解析器：把嵌套结构**展平成「路径: 值」语义行**（保留键路径含义），
 * 再按**顶层分支**（根对象的每个键 / 根数组的每个元素）分组为片段，
 * 定位串即该分支的 JSON 路径（如 `$.server.port`、`$[3].name`）。
 *
 * 内容过多时按 `kMaxLinesPerSegment` 截断，并在片段尾部明确标注，**不静默丢数据**。
 */
class JsonExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;

    /// 单个片段最多的语义行数：超出则截断并标注
    static constexpr int kMaxLinesPerSegment = 20000;
};
