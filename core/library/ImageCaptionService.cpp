#include "core/library/ImageCaptionService.h"

#include "core/library/extract/ImageCaptionExtractor.h"
#include "providers/AiProvider.h"

namespace {

/// 通用档：完整描述 + **强制逐字抄录**（一次调用同时拿描述与文字，不单独再接 OCR）
const char *kPromptCommonTail =
    "2) 若图片中含有任何文字（标题 / 正文 / 表格 / 截图界面 / 注释 / 水印），"
    "必须**逐字完整抄录**，保留原有分行与表格的行列关系；不要翻译、不要概括、"
    "不要漏字、不要用「等等」省略。若确认无文字，写「无可见文字」。\n"
    "3) 最后单独一行给出 3-8 个用于后续检索的关键词。\n"
    "只输出上述内容，不要客套话，不要复述本要求。";

/// 文字密集档（扫描件 / 表格照片 / 截图）：以逐字抄录为第一优先，描述从简
const char *kPromptTextHeavy =
    "这是一张以文字为主的图片（扫描件 / 表格照片 / 截图）。请严格按以下顺序输出：\n"
    "1) 先**逐字完整抄录**图中所有文字，保留原有分行与表格的行列关系；"
    "不要翻译、不要概括、不要漏字、不要用「等等」省略。若确认无文字，写「无可见文字」。\n"
    "2) 再用一句话说明图片的整体类型与用途。\n"
    "3) 最后单独一行给出 3-8 个用于后续检索的关键词。\n"
    "只输出上述内容，不要客套话，不要复述本要求。";

QString captionPrompt(const QString &profile)
{
    if (profile == QStringLiteral("text-heavy")) {
        return QString::fromUtf8(kPromptTextHeavy);
    }
    return QStringLiteral("你是资料库入库助手，请对这张图片输出结构化描述，供后续文字检索使用：\n"
                          "1) 先用一句话说明图片整体内容（主体、场景、类型）。\n")
        + QString::fromUtf8(kPromptCommonTail);
}

}  // namespace

ImageCaptionService::ImageCaptionService(AiProvider *provider, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
{
    if (m_provider) {
        connect(m_provider, &AiProvider::chatFinished, this, &ImageCaptionService::onProviderFinished);
        connect(m_provider, &AiProvider::chatFailed, this, &ImageCaptionService::onProviderFailed);
    }
}

void ImageCaptionService::setCaptionConfig(const LibraryCaptionConfig &config)
{
    m_caption = config;
}

LibraryCaptionConfig ImageCaptionService::captionConfig() const
{
    return m_caption;
}

void ImageCaptionService::setConsentGiven(bool given)
{
    m_consentGiven = given;
}

bool ImageCaptionService::consentGiven() const
{
    return m_consentGiven;
}

bool ImageCaptionService::isReady() const
{
    return m_provider && m_provider->isConfigured() && !m_caption.modelId.isEmpty();
}

bool ImageCaptionService::isIdle() const
{
    return !m_busy && m_queue.isEmpty();
}

void ImageCaptionService::captionItem(qint64 fileId, const QString &path, int imageOrdinal)
{
    Item item;
    item.fileId = fileId;
    item.path = path;
    item.imageOrdinal = imageOrdinal > 0 ? imageOrdinal : 1;
    m_queue.enqueue(item);
    processNext();
}

void ImageCaptionService::cancelAll()
{
    const bool working = m_busy;
    m_queue.clear();
    if (working) {
        if (m_provider) {
            m_provider->cancel();
        }
        // provider 若同步回 Cancelled，这里已经是空闲；否则兜底结束在途项
        if (m_busy) {
            ChatError error;
            error.code = ChatError::Cancelled;
            error.message = QStringLiteral("图片描述已取消，该图片标记为待索引");
            failCurrent(error);
        }
        return;  // queueDrained 由 finishCurrent 统一发出
    }
    emit queueDrained();
}

void ImageCaptionService::processNext()
{
    if (m_busy) {
        return;
    }

    while (!m_queue.isEmpty()) {
        const Item item = m_queue.dequeue();

        // provider 未配置 / 未指定 caption 模型：**整批**结构化失败，不崩、不阻断
        //（上层会在配置好视觉模型后重试）
        if (!isReady()) {
            ChatError error;
            error.code = ChatError::NotConfigured;
            error.message = QStringLiteral("尚未配置可用于图片描述的视觉模型，该图片已标记为待索引");
            emit captionFailed(item.fileId, error);
            continue;
        }

        // 用户未显式同意：一个请求都不发（硬性约束，杜绝静默上传）
        if (!m_consentGiven) {
            ChatError error;
            error.code = ChatError::ImagesNotConfirmed;
            error.message = QStringLiteral("图片外发未获用户确认：该图片未上传，已标记为待索引");
            emit captionFailed(item.fileId, error);
            continue;
        }

        // 本地转码（此处失败属本地问题，message 里明确写「本地转码失败」以免被当作网络故障）
        const ImageCaptionExtractor extractor(m_caption.maxEdge);
        ExtractionResult local;
        QString localError;
        if (!extractor.extract(item.path, &local, &localError) || local.captionTasks.isEmpty()) {
            ChatError error;
            error.code = ChatError::Unknown;
            error.message = QStringLiteral("图片本地转码失败：%1").arg(localError);
            emit captionFailed(item.fileId, error);
            continue;
        }
        const ImageCaptionTask task = local.captionTasks.first();

        ChatMessage message;
        message.role = ChatMessage::User;
        message.text = captionPrompt(m_caption.promptProfile);
        ChatImage image;
        image.bytes = task.bytes;  // 只发转码后副本，不发原图
        image.mimeType = task.mimeType;
        message.images.append(image);

        ChatRequest request;
        request.model = m_caption.modelId;
        request.messages.append(message);
        request.temperature = 0.2;  // 描述/抄录属事实性任务，压低随机性
        request.maxTokens = m_caption.maxTokens;
        request.stream = false;     // 一次拿完整文本，无需增量渲染
        request.imagesConfirmed = true;  // 已在上面确认过用户同意，否则不会走到这里
        // thinkingLevel / nativeParams 留空：caption 不需要思考参数，避免额外开销

        m_current = item;
        m_busy = true;
        m_provider->sendChat(request);
        return;  // 等信号回来再继续队列
    }

    emit queueDrained();
}

void ImageCaptionService::onProviderFinished(const ChatResponse &response)
{
    if (!m_busy) {
        return;  // 非本服务发起的请求（如对话），一律忽略
    }
    if (!response.ok || response.error.isError()) {
        ChatError error = response.error;
        if (!error.isError()) {
            error.code = ChatError::Unknown;
            error.message = QStringLiteral("图片描述请求失败");
        }
        failCurrent(error);
        return;
    }

    const QString caption = response.text.trimmed();
    if (caption.isEmpty()) {
        ChatError error;
        error.code = ChatError::EmptyResponse;
        error.message = QStringLiteral("视觉模型未返回有效描述，该图片已标记为待索引");
        failCurrent(error);
        return;
    }

    emit captionReady(m_current.fileId, m_current.imageOrdinal, caption, response.usage);
    finishCurrent();
}

void ImageCaptionService::onProviderFailed(const ChatError &error)
{
    if (!m_busy) {
        return;
    }
    failCurrent(error);
}

void ImageCaptionService::failCurrent(const ChatError &error)
{
    emit captionFailed(m_current.fileId, error);
    finishCurrent();
}

void ImageCaptionService::finishCurrent()
{
    m_busy = false;
    m_current = Item();

    if (m_queue.isEmpty()) {
        emit queueDrained();
        return;
    }
    // 用排队调用而非直接递归：即使 provider 同轮回信号，栈深也不会随队列长度增长
    QMetaObject::invokeMethod(this, "processNext", Qt::QueuedConnection);
}
