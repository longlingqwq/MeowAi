#pragma once

#include "core/retriever/ChunkSplitter.h"
#include "storage/LibraryStore.h"

#include <QList>
#include <QString>

/// 某个章节在文件内的片段区间
struct DetectedSection
{
    QString title;
    int chunkStart = 0;
    int chunkEnd = 0;
    int tokenEst = 0;
};

/// 文档级章节元数据（落库到 documents / sections 两表）
struct DocumentMeta
{
    QString sectionSource;   ///< heading / sheet / slide / page_approx / none
    int tokenEstTotal = 0;
    QList<DetectedSection> sections;
};

/**
 * 章节识别：把片段序列划分成章节。
 *
 * 优先级：
 *   1. `headings` 非空（docx 样式标题 / markdown `#`）→ 按标题划分，`sectionSource = heading`
 *   2. 否则按主定位类型：sheet（工作表）、slide（幻灯片）、page（PDF 页近似）、
 *      json（顶层分支），`sectionSource` 依次为 sheet / slide / page_approx / page_approx
 *   3. 其余（para / row / image / none）不划分章节，`sectionSource = none`
 *
 * 纯函数、不访问数据库，便于单测。
 */
DocumentMeta detectDocumentMeta(const QList<ChunkRecord> &chunks,
                                const QList<HeadingSpan> &headings = QList<HeadingSpan>());
