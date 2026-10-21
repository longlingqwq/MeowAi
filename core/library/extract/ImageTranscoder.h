#pragma once

#include "core/library/extract/Extractor.h"

#include <QByteArray>
#include <QString>

/**
 * 图片本地转码器（模块 M3-d）。
 *
 * 职责边界：**纯本地、同步、绝不联网、绝不写盘、绝不改动原图**。只做三件事：
 *  1. 校验（存在 / 普通文件 / 未超 `kMaxExtractBytes`）；
 *  2. 解码（EXIF 方向校正；拒绝像素过大的「解压炸弹」）；
 *  3. 长边压缩到 `maxEdge`（默认 1536，显著降低视觉 token）后统一编码为 PNG/JPEG。
 *
 * 为什么需要它：caption 要外发图片，直接发原图既贵（token）又不必要；压缩后外发，
 * 并让「外发字节」有统一口径（编码、尺寸都可预期）。
 */
class ImageTranscoder
{
public:
    /// 默认长边上限；与 `LibraryCaptionConfig::maxEdge` 的默认值同源
    static constexpr int kDefaultMaxEdge = 1536;

    /// 解码阶段的像素上限（6400 万像素 ≈ 256 MB 位图）：超过即拒绝，防解压炸弹吃满内存
    static constexpr qint64 kMaxSourcePixels = 64LL * 1000 * 1000;

    /**
     * 转码为可外发的 caption 任务。
     *  - `maxEdge <= 0` 时使用 `kDefaultMaxEdge`；
     *  - 多帧图（如 GIF）**只取首帧**，不产多片段；
     *  - 「图片序号」由上层在 caption 阶段赋予（本层只负责图片本身，不参与排序）。
     *
     * 失败返回 false 并写 `error`（含具体原因），绝不抛异常。
     */
    static bool transcode(const QString &path, int maxEdge, ImageCaptionTask *task, QString *error);
};
