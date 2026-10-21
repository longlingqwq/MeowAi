#pragma once

#include "core/library/extract/Extractor.h"
#include "core/library/extract/ImageTranscoder.h"

/**
 * 图片类解析器（模块 M3-d）。
 *
 * 图片不做本地 OCR：入库文本必须来自视觉模型。因此本类**只做本地部分**——
 * 校验 + 压缩转码（见 `ImageTranscoder`），产出 `ImageCaptionTask`；
 * `ExtractionResult::segments` 为空、`captionTasks` 一条。
 *
 * 真正的 caption 请求由上层（`ImageCaptionService`）在用户**显式同意**后经 providers 层发出，
 * 拿到描述文本后再回填为「图片 #N」定位片段。本类不联网、不写盘、不改动原图。
 */
class ImageCaptionExtractor : public Extractor
{
public:
    /// `maxEdge <= 0` 时使用 `ImageTranscoder::kDefaultMaxEdge`；
    /// 上层按 `LibraryCaptionConfig::maxEdge` 构造，保证「转码口径」只有一处
    explicit ImageCaptionExtractor(int maxEdge = ImageTranscoder::kDefaultMaxEdge);

    QString id() const override;
    bool supports(LibraryKind kind, const QString &extension) const override;

    /**
     * 本地部分：转码成功 → `ok = true` + 一个 `captionTask`。
     * `segments` 为空是**预期**而非缺陷：入库文本必须由视觉模型给出，本地不产文本、不臆造。
     */
    bool extract(const QString &path, ExtractionResult *result, QString *error) const override;

    /// 视觉模型返回描述后由上层组装入库片段：`loc_kind = image`、定位串「图片 #N」
    static ExtractedSegment segmentFromCaption(int imageOrdinal, const QString &caption);

private:
    int m_maxEdge;
};
