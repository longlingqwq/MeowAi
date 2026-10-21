#include "core/library/extract/ExtractorRegistry.h"

#include "core/library/extract/CsvExtractor.h"
#include "core/library/extract/DocxExtractor.h"
#include "core/library/extract/ImageCaptionExtractor.h"
#include "core/library/extract/JsonExtractor.h"
#include "core/library/extract/PdfTextExtractor.h"
#include "core/library/extract/PlainTextExtractor.h"
#include "core/library/extract/PptxExtractor.h"
#include "core/library/extract/VideoExtractor.h"
#include "core/library/extract/XlsxExtractor.h"

#include <algorithm>

ExtractorRegistry::ExtractorRegistry()
{
    add(std::make_unique<PlainTextExtractor>());
    add(std::make_unique<CsvExtractor>());
    add(std::make_unique<JsonExtractor>());
    add(std::make_unique<PdfTextExtractor>());
    add(std::make_unique<DocxExtractor>());
    add(std::make_unique<XlsxExtractor>());
    add(std::make_unique<PptxExtractor>());
    // 图片：本地产出待外发的 caption 任务（segments 为空），描述由上层经 providers 层补齐
    add(std::make_unique<ImageCaptionExtractor>());
    add(std::make_unique<VideoExtractor>());
}

void ExtractorRegistry::add(std::unique_ptr<Extractor> extractor)
{
    if (!extractor) {
        return;
    }
    const QString newId = extractor->id();
    const auto existing = std::find_if(
        m_extractors.begin(), m_extractors.end(),
        [&newId](const std::unique_ptr<Extractor> &item) { return item && item->id() == newId; });
    if (existing != m_extractors.end()) {
        *existing = std::move(extractor);
        return;
    }
    m_extractors.push_back(std::move(extractor));
}

Extractor *ExtractorRegistry::forPath(const QString &path) const
{
    const LibraryKind kind = libraryKindForPath(path);
    if (kind == LibraryKind::Unknown) {
        return nullptr;
    }
    const QString extension = libraryExtension(path);
    for (const std::unique_ptr<Extractor> &item : m_extractors) {
        if (item && item->supports(kind, extension)) {
            return item.get();
        }
    }
    return nullptr;
}

QStringList ExtractorRegistry::ids() const
{
    QStringList list;
    for (const std::unique_ptr<Extractor> &item : m_extractors) {
        if (item) {
            list.append(item->id());
        }
    }
    return list;
}

bool ExtractorRegistry::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }
    if (error) {
        error->clear();
    }

    Extractor *extractor = forPath(path);
    if (!extractor) {
        if (error) {
            *error = QStringLiteral("暂不支持该格式的解析：.%1").arg(libraryExtension(path));
        }
        return false;
    }
    return extractor->extract(path, result, error);
}
