#pragma once

#include <QString>

/**
 * 资料库文件的类别判定（小库/文件管理与解析器注册表共用）。
 *
 * 只负责「按扩展名归类」，不含任何解析能力：具体如何抽取文本由 `extract/` 层决定。
 * 归类口径与 `docs/PROMPT.md` 的本次范围一致：
 *  - text ：txt / md / csv / json / PDF（文本层）/ docx / xlsx / pptx
 *  - image：jpg / jpeg / png / webp / gif / bmp（入库时走多模态 caption）
 *  - video：mp4 / mov / mkv / avi / webm / m4v（**本次仅预留、不索引**）
 *  - 其余扩展名一律 Unknown → 不入库
 */
enum class LibraryKind
{
    Unknown,
    Text,
    Image,
    Video,
};

/// 按路径扩展名判定类别（大小写不敏感，含 `.` 或不含均可）
LibraryKind libraryKindForPath(const QString &path);

/// 落库用的类别字符串：text / image / video（Unknown 返回空串）
QString libraryKindName(LibraryKind kind);

/// 小写、不含点、不含路径的扩展名；无扩展名返回空串
QString libraryExtension(const QString &path);

/// 该扩展名本次是否可解析（video 不算：本次不实现解析）
bool libraryKindIsIndexable(LibraryKind kind);
