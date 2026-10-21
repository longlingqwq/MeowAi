#include "core/library/ImageCaptionService.h"
#include "core/library/extract/ExtractorRegistry.h"
#include "core/library/extract/ImageCaptionExtractor.h"
#include "core/library/extract/ImageTranscoder.h"
#include "providers/AiProvider.h"

#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <QTemporaryDir>
#include <QtTest>

namespace {

/// 现场生成图片夹具（不提交任何二进制文件）；失败返回空串
QString writeImage(const QString &path, int width, int height, const char *format)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(28, 110, 180));

    QPainter painter(&image);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter,
                     QStringLiteral("MeowAi %1x%2").arg(width).arg(height));
    painter.end();

    return image.save(path, format) ? path : QString();
}

/// 解码字节流取真实尺寸：用于证明「外发的是转码后副本」而不是原图
QSize decodedSize(const QByteArray &bytes)
{
    QImage image;
    return image.loadFromData(bytes) ? image.size() : QSize();
}

}  // namespace

/// 假 Provider：**不发任何网络请求**，只记录请求形态并按脚本同步回信号
class FakeAiProvider : public AiProvider
{
    Q_OBJECT

public:
    explicit FakeAiProvider(QObject *parent = nullptr) : AiProvider(parent) {}

    Protocol protocol() const override { return Protocol::OpenAI; }
    QString providerId() const override { return QStringLiteral("fake"); }
    bool isConfigured() const override { return m_configured; }
    void configure(const ProviderConfig &config) override { m_configured = !config.apiKey.isEmpty(); }

    void sendChat(const ChatRequest &request) override
    {
        m_requests.append(request);
        if (m_error.isError()) {
            emit chatFailed(m_error);
            return;
        }
        ChatResponse response;
        response.ok = true;
        response.text = m_text;
        response.model = request.model;
        response.usage = m_usage;
        emit chatFinished(response);
    }

    void cancel() override {}
    void validateCredentials() override {}
    void fetchModels() override {}

    void setReplyText(const QString &text) { m_text = text; }
    void setReplyUsage(const ChatUsage &usage) { m_usage = usage; }
    void setNextError(const ChatError &error) { m_error = error; }
    void clearNextError() { m_error = ChatError(); }

    QList<ChatRequest> requests() const { return m_requests; }
    ChatRequest lastRequest() const
    {
        return m_requests.isEmpty() ? ChatRequest() : m_requests.last();
    }

private:
    QList<ChatRequest> m_requests;
    bool m_configured = true;
    QString m_text = QStringLiteral("蓝色示意图，含文字「MeowAi」");
    ChatUsage m_usage;
    ChatError m_error;
};

class TstImageCapture : public QObject
{
    Q_OBJECT

private slots:
    void transcodeShrinksLongEdgeKeepingAspect();
    void transcodeKeepsSmallImageUntouched();
    void transcodeNormalizesBmpToGenericFormat();
    void transcodeReportsMissingAndUndecodableFiles();
    void extractorYieldsCaptionTaskWithoutText();
    void segmentFromCaptionCarriesImageLocator();
    void captionRefusesWithoutConsent();
    void captionSendsOneRequestWithImageAndForcedTranscription();
    void captionSupportsTextHeavyProfile();
    void captionWithoutVisionModelFailsAsNotConfigured();
    void captionForwardsRateLimitAndContinuesQueue();
};

void TstImageCapture::transcodeShrinksLongEdgeKeepingAspect()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = writeImage(tmp.filePath(QStringLiteral("宽图.png")), 3000, 1000, "PNG");
    QVERIFY(!path.isEmpty());

    ImageCaptionTask task;
    QString error;
    QVERIFY2(ImageTranscoder::transcode(path, 1536, &task, &error), qPrintable(error));

    QCOMPARE(task.width, 1536);
    QCOMPARE(task.height, 512);  // 3000:1000 = 3:1，等比缩放
    QCOMPARE(task.mimeType, QStringLiteral("image/png"));
    QCOMPARE(task.sourcePath, path);
    QVERIFY(task.isValid());
    // 转码产物本身可解码，且确实被压到上限
    QCOMPARE(decodedSize(task.bytes), QSize(1536, 512));
}

void TstImageCapture::transcodeKeepsSmallImageUntouched()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = writeImage(tmp.filePath(QStringLiteral("小图.jpg")), 800, 600, "JPG");
    QVERIFY(!path.isEmpty());

    ImageCaptionTask task;
    QString error;
    QVERIFY2(ImageTranscoder::transcode(path, 1536, &task, &error), qPrintable(error));

    // 只缩不放：小于上限的图片保持原尺寸
    QCOMPARE(task.width, 800);
    QCOMPARE(task.height, 600);
    QCOMPARE(decodedSize(task.bytes), QSize(800, 600));
}

void TstImageCapture::transcodeNormalizesBmpToGenericFormat()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = writeImage(tmp.filePath(QStringLiteral("面板图.bmp")), 1000, 500, "BMP");
    QVERIFY2(!path.isEmpty(), "Qt 运行库应内置 BMP 写入支持");

    ImageCaptionTask task;
    QString error;
    QVERIFY2(ImageTranscoder::transcode(path, 1000, &task, &error), qPrintable(error));

    // BMP 归一到外发通用格式，绝不把 BMP 原样发出去
    QVERIFY(task.mimeType == QStringLiteral("image/png")
            || task.mimeType == QStringLiteral("image/jpeg"));
    QCOMPARE(task.width, 1000);
    QCOMPARE(task.height, 500);
    QCOMPARE(decodedSize(task.bytes), QSize(1000, 500));
}

void TstImageCapture::transcodeReportsMissingAndUndecodableFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ImageCaptionTask task;

    // 1) 文件不存在
    QString error;
    QVERIFY(!ImageTranscoder::transcode(tmp.filePath(QStringLiteral("没有这张图.png")), 1536, &task,
                                        &error));
    QVERIFY(error.contains(QStringLiteral("不存在")));

    // 2) 内容根本不是图片（扩展名骗不过解码器）
    const QString fake = tmp.filePath(QStringLiteral("假图.png"));
    QFile file(fake);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("这不是图片内容"));
    file.close();

    error.clear();
    QVERIFY(!ImageTranscoder::transcode(fake, 1536, &task, &error));
    QVERIFY(error.contains(QStringLiteral("无法解码")));
    QVERIFY(!task.isValid());
}

void TstImageCapture::extractorYieldsCaptionTaskWithoutText()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = writeImage(tmp.filePath(QStringLiteral("照片.png")), 600, 400, "PNG");
    QVERIFY(!path.isEmpty());

    // 经注册点路由：图片与文本同样「在册」
    const ExtractorRegistry registry;
    QCOMPARE(registry.forPath(path)->id(), QStringLiteral("image"));

    ExtractionResult result;
    QString error;
    QVERIFY2(registry.extract(path, &result, &error), qPrintable(error));
    QVERIFY(result.ok);
    QVERIFY(result.segments.isEmpty());  // 本地不产文本（描述须由视觉模型给出）
    QCOMPARE(result.captionTasks.size(), 1);
    QVERIFY(result.captionTasks.first().isValid());
    QCOMPARE(result.captionTasks.first().mimeType, QStringLiteral("image/png"));

    // 长边上限由上层注入（配置项）：image 解析器构造时生效
    const ImageCaptionExtractor tight(400);
    ExtractionResult tightResult;
    error.clear();
    QVERIFY2(tight.extract(path, &tightResult, &error), qPrintable(error));
    QCOMPARE(tightResult.captionTasks.first().width, 400);
    QCOMPARE(tightResult.captionTasks.first().height, 266);  // 600:400 = 3:2，等比取整

    // 类别隔离：图片解析器不认领视频 / 文本
    QVERIFY(!tight.supports(LibraryKind::Video, QStringLiteral("mp4")));
    QVERIFY(!tight.supports(LibraryKind::Text, QStringLiteral("txt")));
    QVERIFY(!tight.supports(LibraryKind::Unknown, QString()));
}

void TstImageCapture::segmentFromCaptionCarriesImageLocator()
{
    const ExtractedSegment segment =
        ImageCaptionExtractor::segmentFromCaption(3, QStringLiteral("  门禁系统面板照片  "));

    QCOMPARE(segment.locKind, LocatorKind::Image);
    QCOMPARE(segment.locLabel, QStringLiteral("图片 #3"));
    QCOMPARE(segment.text, QStringLiteral("门禁系统面板照片"));
}

void TstImageCapture::captionRefusesWithoutConsent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeImage(tmp.filePath(QStringLiteral("照片.png")), 400, 300, "PNG");
    QVERIFY(!path.isEmpty());

    FakeAiProvider provider;
    ImageCaptionService service(&provider);

    LibraryCaptionConfig config;
    config.modelId = QStringLiteral("qwen-vl-max");
    service.setCaptionConfig(config);
    service.setConsentGiven(false);  // 默认即 false
    QVERIFY(service.isReady());

    int failedCount = 0;
    ChatError captured;
    connect(&service, &ImageCaptionService::captionFailed, &service,
            [&](qint64, const ChatError &capturedError) {
                ++failedCount;
                captured = capturedError;
            });

    service.captionItem(7, path, 1);

    QCOMPARE(failedCount, 1);
    QCOMPARE(captured.code, ChatError::ImagesNotConfirmed);
    QVERIFY(captured.message.contains(QStringLiteral("未获用户确认")));
    QVERIFY(provider.requests().isEmpty());  // 硬性约束：一个请求都不许发
    QVERIFY(service.isIdle());
}

void TstImageCapture::captionSendsOneRequestWithImageAndForcedTranscription()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeImage(tmp.filePath(QStringLiteral("面板.png")), 1200, 900, "PNG");
    QVERIFY(!path.isEmpty());

    FakeAiProvider provider;
    provider.setReplyText(QStringLiteral("  门禁面板照片，含文字「访客通道」。\n关键词：门禁、访客  "));
    ChatUsage usage;
    usage.promptTokens = 1200;
    usage.completionTokens = 80;
    usage.totalTokens = 1280;
    provider.setReplyUsage(usage);

    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.modelId = QStringLiteral("qwen-vl-max");
    config.maxEdge = 512;
    config.maxTokens = 300;
    service.setCaptionConfig(config);
    service.setConsentGiven(true);

    int readyCount = 0;
    int ordinal = 0;
    QString caption;
    ChatUsage gotUsage;
    connect(&service, &ImageCaptionService::captionReady, &service,
            [&](qint64, int imageOrdinal, const QString &text, const ChatUsage &reportedUsage) {
                ++readyCount;
                ordinal = imageOrdinal;
                caption = text;
                gotUsage = reportedUsage;
            });

    service.captionItem(42, path, 3);

    QCOMPARE(readyCount, 1);
    QCOMPARE(ordinal, 3);
    QCOMPARE(caption, QStringLiteral("门禁面板照片，含文字「访客通道」。\n关键词：门禁、访客"));  // 已 trim
    QCOMPARE(gotUsage.totalTokens, 1280);
    QVERIFY(service.isIdle());

    // 一次调用（不额外接 OCR）：请求形态逐项核对
    QCOMPARE(provider.requests().size(), 1);
    const ChatRequest request = provider.lastRequest();
    QCOMPARE(request.model, QStringLiteral("qwen-vl-max"));
    QCOMPARE(request.stream, false);          // 一次拿完整文本
    QVERIFY(request.imagesConfirmed);         // 下发时携带显式确认
    QCOMPARE(request.maxTokens, 300);
    QCOMPARE(request.messages.size(), 1);

    const ChatMessage &message = request.messages.first();
    QCOMPARE(message.images.size(), 1);
    QVERIFY(!message.images.first().bytes.isEmpty());
    QCOMPARE(message.images.first().mimeType, QStringLiteral("image/png"));
    // 外发的是**转码后副本**（原图 1200x900，上限 512）
    QCOMPARE(decodedSize(message.images.first().bytes), QSize(512, 384));
    // 提示词同时要求「描述」与「强制逐字抄录」
    QVERIFY(message.text.contains(QStringLiteral("逐字完整抄录")));
    QVERIFY(message.text.contains(QStringLiteral("关键词")));
}

void TstImageCapture::captionSupportsTextHeavyProfile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeImage(tmp.filePath(QStringLiteral("扫描件.png")), 500, 500, "PNG");
    QVERIFY(!path.isEmpty());

    FakeAiProvider provider;
    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.modelId = QStringLiteral("glm-4v");
    config.promptProfile = QStringLiteral("text-heavy");
    service.setCaptionConfig(config);
    service.setConsentGiven(true);

    service.captionItem(1, path, 1);

    QCOMPARE(provider.requests().size(), 1);
    const QString prompt = provider.lastRequest().messages.first().text;
    QVERIFY(prompt.contains(QStringLiteral("以文字为主的图片")));
    QVERIFY(prompt.contains(QStringLiteral("逐字完整抄录")));
}

void TstImageCapture::captionWithoutVisionModelFailsAsNotConfigured()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeImage(tmp.filePath(QStringLiteral("照片.png")), 300, 300, "PNG");
    QVERIFY(!path.isEmpty());

    // 情形一：未指定 caption 模型
    FakeAiProvider provider;
    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    service.setCaptionConfig(config);
    service.setConsentGiven(true);
    QVERIFY(!service.isReady());

    QList<ChatError> failures;
    connect(&service, &ImageCaptionService::captionFailed, &service,
            [&](qint64, const ChatError &error) { failures.append(error); });

    service.captionItem(1, path, 1);
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.first().code, ChatError::NotConfigured);
    QVERIFY(provider.requests().isEmpty());

    // 情形二：有模型但 provider 未配置（无 Key）：同样结构化失败，不崩
    provider.configure(ProviderConfig());
    QVERIFY(!provider.isConfigured());
    config.modelId = QStringLiteral("qwen-vl-max");
    service.setCaptionConfig(config);
    QVERIFY(!service.isReady());

    service.captionItem(2, path, 1);
    QCOMPARE(failures.size(), 2);
    QCOMPARE(failures.last().code, ChatError::NotConfigured);
    QVERIFY(provider.requests().isEmpty());
}

void TstImageCapture::captionForwardsRateLimitAndContinuesQueue()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = writeImage(tmp.filePath(QStringLiteral("照片.png")), 400, 400, "PNG");
    QVERIFY(!path.isEmpty());

    FakeAiProvider provider;
    ChatError rateLimited;
    rateLimited.code = ChatError::RateLimited;
    rateLimited.httpStatus = 429;
    rateLimited.message = QStringLiteral("请求过于频繁");
    provider.setNextError(rateLimited);

    ImageCaptionService service(&provider);
    LibraryCaptionConfig config;
    config.modelId = QStringLiteral("qwen-vl-max");
    service.setCaptionConfig(config);
    service.setConsentGiven(true);

    QList<ChatError> failures;
    int readyCount = 0;
    connect(&service, &ImageCaptionService::captionFailed, &service,
            [&](qint64, const ChatError &error) { failures.append(error); });
    connect(&service, &ImageCaptionService::captionReady, &service,
            [&](qint64, int, const QString &, const ChatUsage &) { ++readyCount; });

    service.captionItem(1, path, 1);

    // 429 原样结构化回传：上层据此「待索引 + 稍后重试」，绝不当作「图片无效」
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.first().code, ChatError::RateLimited);
    QCOMPARE(failures.first().httpStatus, 429);

    // 单张失败不阻断后续：第二张正常成功
    provider.clearNextError();
    service.captionItem(2, path, 2);

    QCOMPARE(failures.size(), 1);
    QCOMPARE(readyCount, 1);
    QCOMPARE(provider.requests().size(), 2);
    QVERIFY(service.isIdle());
}

QTEST_MAIN(TstImageCapture)
#include "tst_imagecapture.moc"
