#pragma once

#include "core/library/extract/Extractor.h"

/**
 * 视频解析器（**预留注册点，本次不实现**）。
 *
 * 按已锁定决策，本次范围不含视频：`LibraryManager` 入库时即把视频登记为
 * `unsupported`、**不会进入索引流程**；本类存在的意义是让 `ExtractorRegistry`
 * 的类型覆盖完整、后续实现视频解析时只需替换本类内部，不动注册与调用方。
 */
class VideoExtractor : public Extractor
{
public:
    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;
};
