#include "core/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace {

// 资源类型白名单与体积上限：拒绝不支持的格式与超大文件，防止拖垮界面
const QStringList kFontExtensions = { QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc") };
const QStringList kImageExtensions = { QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                       QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
                                       QStringLiteral("svg") };
const QStringList kVideoExtensions = { QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("m4v"),
                                       QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("avi") };

/// 字节 → MB 的换算基准：统一 qint64，避免 32 位 int 溢出
constexpr qint64 kBytesPerMb = 1024LL * 1024;

constexpr qint64 kFontLimit = 20LL * kBytesPerMb;   // 20 MB
constexpr qint64 kImageLimit = 128LL * kBytesPerMb; // 128 MB（背景 / 头像与资料库图片统一）
constexpr qint64 kVideoLimit = 200LL * kBytesPerMb; // 200 MB

/// 实际体积向上取整到 MB，避免「128 MB 超出 128 MB」这类含糊提示
qint64 bytesToMbCeil(qint64 bytes)
{
    return bytes <= 0 ? 0 : (bytes + kBytesPerMb - 1) / kBytesPerMb;
}

/// 只保留安全字符，防止导入文件名带出路径穿越或非法字符
QString sanitizeBaseName(const QString &baseName)
{
    QString cleaned;
    cleaned.reserve(baseName.size());
    for (const QChar &ch : baseName) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')) {
            cleaned.append(ch);
        } else {
            cleaned.append(QLatin1Char('_'));
        }
    }
    while (cleaned.startsWith(QLatin1Char('.'))) {
        cleaned.remove(0, 1);
    }
    if (cleaned.isEmpty()) {
        cleaned = QStringLiteral("asset");
    }
    return cleaned.left(48);
}

/// 目标目录内生成不冲突的文件名（同名时追加 -1、-2 …）
QString uniqueTargetPath(const QString &dirPath, const QString &baseName, const QString &extension)
{
    const QDir dir(dirPath);
    const QString suffix = extension.isEmpty() ? QString() : QStringLiteral(".%1").arg(extension);
    QString candidate = baseName + suffix;
    int index = 1;
    while (QFile::exists(dir.filePath(candidate))) {
        candidate = QStringLiteral("%1-%2%3").arg(baseName).arg(index++).arg(suffix);
    }
    return dir.filePath(candidate);
}

/// QML 的 FileDialog 给出 file:// URL；普通路径原样返回
QString localPathFromInput(const QString &input)
{
    const QUrl url(input);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return input;
}

QString kindLabel(bool isFont, bool isVideo)
{
    if (isFont) {
        return QStringLiteral("字体");
    }
    return isVideo ? QStringLiteral("视频") : QStringLiteral("图片");
}

} // namespace

ThemeManager::ThemeManager(AppPaths paths, PersonalizationStore *store, QObject *parent)
    : QObject(parent)
    , m_paths(std::move(paths))
    , m_store(store)
{
    if (QGuiApplication *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        m_defaultFont = gui->font();
    }
}

ThemeManager::~ThemeManager() = default;

QString ThemeManager::fontFamily() const
{
    return m_config.fontFamily;
}

QString ThemeManager::backgroundSource() const
{
    return toSourceUrl(m_config.backgroundSource);
}

QString ThemeManager::avatarSource() const
{
    return toSourceUrl(m_config.avatarSource);
}

QString ThemeManager::splashSource() const
{
    return toSourceUrl(m_config.splashSource);
}

bool ThemeManager::splashEnabled() const
{
    return m_config.splashEnabled;
}

void ThemeManager::setSplashEnabled(bool enabled)
{
    if (m_config.splashEnabled == enabled) {
        return;
    }
    if (!ensureWritable()) {
        return;
    }
    const bool previous = m_config.splashEnabled;
    m_config.splashEnabled = enabled;
    if (!persist()) {
        m_config.splashEnabled = previous; // 保存失败则不生效
        return;
    }
    emit splashChanged();
}

bool ThemeManager::splashSoundEnabled() const
{
    return m_config.splashSoundEnabled;
}

void ThemeManager::setSplashSoundEnabled(bool enabled)
{
    if (m_config.splashSoundEnabled == enabled) {
        return;
    }
    if (!ensureWritable()) {
        return;
    }
    const bool previous = m_config.splashSoundEnabled;
    m_config.splashSoundEnabled = enabled;
    if (!persist()) {
        m_config.splashSoundEnabled = previous;
        return;
    }
    emit splashChanged();
}

bool ThemeManager::available() const
{
    return m_available;
}

QString ThemeManager::lastError() const
{
    return m_lastError;
}

void ThemeManager::load()
{
    if (!m_store || m_paths.root().trimmed().isEmpty()) {
        setAvailable(false);
        setLastError(QStringLiteral("个性化配置不可用：本地数据目录或配置存储未就绪"));
        emitAllChanged();
        return;
    }
    setAvailable(true);

    QString error;
    PersonalizationConfig config = m_store->load(&error);

    // 资源存在性校验：文件缺失时回落默认外观，绝不引用不存在的文件
    QString missing;
    auto checkImage = [&](QString &relative) {
        if (!relative.isEmpty() && !QFile::exists(absolutePath(relative))) {
            missing = relative;
            relative.clear();
        }
    };
    checkImage(config.backgroundSource);
    checkImage(config.avatarSource);
    checkImage(config.splashSource);
    if (!config.fontSource.isEmpty() && !QFile::exists(absolutePath(config.fontSource))) {
        missing = config.fontSource;
        config.fontSource.clear();
        config.fontFamily.clear(); // 导入字体丢失后，族名不再有效
    }

    m_config = config;
    registerImportedFont();
    applyFontFamily();

    if (!missing.isEmpty()) {
        setLastError(QStringLiteral("部分个性化资源已丢失，已回退默认：%1").arg(missing));
    } else {
        setLastError(error);
    }
    emitAllChanged();
}

QStringList ThemeManager::availableFontFamilies() const
{
    // 无图形应用实例（如无界面单元测试）时不存在字体数据库，返回空列表
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return QStringList();
    }
    return QFontDatabase::families();
}

bool ThemeManager::setSystemFont(const QString &family)
{
    if (!ensureWritable()) {
        return false;
    }

    const QString trimmed = family.trimmed();
    const QString previousFamily = m_config.fontFamily;
    const QString previousSource = m_config.fontSource;

    if (trimmed.isEmpty()) {
        m_config.fontSource.clear();
        m_config.fontFamily.clear();
    } else {
        if (!availableFontFamilies().contains(trimmed)) {
            setLastError(QStringLiteral("系统未安装该字体：%1").arg(trimmed));
            return false;
        }
        m_config.fontSource.clear();
        m_config.fontFamily = trimmed;
    }

    applyFontFamily();
    if (!persist()) {
        m_config.fontFamily = previousFamily;
        m_config.fontSource = previousSource;
        applyFontFamily();
        return false;
    }
    removeAsset(previousSource); // 保存成功后再清理旧的自定义字体文件
    emit fontFamilyChanged();
    return true;
}

bool ThemeManager::importFont(const QString &sourcePath)
{
    if (!ensureWritable()) {
        return false;
    }

    QString relative;
    QString error;
    if (!importAsset(sourcePath, AssetKind::Font, QStringLiteral("fonts"), &relative, &error)) {
        setLastError(error);
        return false;
    }

    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        removeAsset(relative);
        setLastError(QStringLiteral("当前运行环境不支持注册字体，已取消导入"));
        return false;
    }

    const int fontId = QFontDatabase::addApplicationFont(absolutePath(relative));
    const QStringList families = fontId >= 0 ? QFontDatabase::applicationFontFamilies(fontId)
                                             : QStringList();
    if (families.isEmpty()) {
        removeAsset(relative);
        setLastError(QStringLiteral("字体文件无法被系统识别，已取消导入"));
        return false;
    }

    const QString previousFamily = m_config.fontFamily;
    const QString previousSource = m_config.fontSource;
    m_config.fontSource = relative;
    m_config.fontFamily = families.first();
    applyFontFamily();
    if (!persist()) {
        m_config.fontFamily = previousFamily;
        m_config.fontSource = previousSource;
        applyFontFamily();
        removeAsset(relative);
        return false;
    }
    removeAsset(previousSource);
    emit fontFamilyChanged();
    return true;
}

bool ThemeManager::setBackground(const QString &sourcePath)
{
    if (!ensureWritable()) {
        return false;
    }

    QString relative;
    QString error;
    if (!importAsset(sourcePath, AssetKind::Image, QStringLiteral("backgrounds"), &relative, &error)) {
        setLastError(error);
        return false;
    }

    const QString previous = m_config.backgroundSource;
    m_config.backgroundSource = relative;
    if (!persist()) {
        m_config.backgroundSource = previous;
        removeAsset(relative);
        return false;
    }
    removeAsset(previous);
    emit backgroundChanged();
    return true;
}

bool ThemeManager::setAvatar(const QString &sourcePath)
{
    if (!ensureWritable()) {
        return false;
    }

    QString relative;
    QString error;
    if (!importAsset(sourcePath, AssetKind::Image, QStringLiteral("avatars"), &relative, &error)) {
        setLastError(error);
        return false;
    }

    const QString previous = m_config.avatarSource;
    m_config.avatarSource = relative;
    if (!persist()) {
        m_config.avatarSource = previous;
        removeAsset(relative);
        return false;
    }
    removeAsset(previous);
    emit avatarChanged();
    return true;
}

bool ThemeManager::setSplash(const QString &sourcePath)
{
    if (!ensureWritable()) {
        return false;
    }

    QString relative;
    QString error;
    if (!importAsset(sourcePath, AssetKind::Video, QStringLiteral("splash"), &relative, &error)) {
        setLastError(error);
        return false;
    }

    const QString previousSource = m_config.splashSource;
    const bool previousEnabled = m_config.splashEnabled;
    m_config.splashSource = relative;
    m_config.splashEnabled = true; // 选定开屏视频后默认开启播放
    if (!persist()) {
        m_config.splashSource = previousSource;
        m_config.splashEnabled = previousEnabled;
        removeAsset(relative);
        return false;
    }
    removeAsset(previousSource);
    emit splashChanged();
    return true;
}

bool ThemeManager::clearFont()
{
    if (!ensureWritable()) {
        return false;
    }

    const QString previousFamily = m_config.fontFamily;
    const QString previousSource = m_config.fontSource;
    m_config.fontSource.clear();
    m_config.fontFamily.clear();
    applyFontFamily();
    if (!persist()) {
        m_config.fontFamily = previousFamily;
        m_config.fontSource = previousSource;
        applyFontFamily();
        return false;
    }
    removeAsset(previousSource);
    emit fontFamilyChanged();
    return true;
}

bool ThemeManager::clearBackground()
{
    if (!ensureWritable()) {
        return false;
    }

    const QString previous = m_config.backgroundSource;
    m_config.backgroundSource.clear();
    if (!persist()) {
        m_config.backgroundSource = previous;
        return false;
    }
    removeAsset(previous);
    emit backgroundChanged();
    return true;
}

bool ThemeManager::clearAvatar()
{
    if (!ensureWritable()) {
        return false;
    }

    const QString previous = m_config.avatarSource;
    m_config.avatarSource.clear();
    if (!persist()) {
        m_config.avatarSource = previous;
        return false;
    }
    removeAsset(previous);
    emit avatarChanged();
    return true;
}

bool ThemeManager::clearSplash()
{
    if (!ensureWritable()) {
        return false;
    }

    // 只清除自定义视频，保留开屏开关：splashSource 为空时自动回落到内置开屏
    const QString previousSource = m_config.splashSource;
    m_config.splashSource.clear();
    if (!persist()) {
        m_config.splashSource = previousSource;
        return false;
    }
    removeAsset(previousSource);
    emit splashChanged();
    return true;
}

bool ThemeManager::resetAll()
{
    if (!ensureWritable()) {
        return false;
    }

    const PersonalizationConfig previous = m_config;
    m_config = PersonalizationConfig{};
    applyFontFamily();
    if (!persist()) {
        m_config = previous;
        applyFontFamily();
        return false;
    }
    removeAsset(previous.fontSource);
    removeAsset(previous.backgroundSource);
    removeAsset(previous.avatarSource);
    removeAsset(previous.splashSource);
    emitAllChanged();
    return true;
}

bool ThemeManager::importAsset(const QString &sourcePath, AssetKind kind, const QString &subDir,
                               QString *relativePath, QString *error) const
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (error) {
        error->clear();
    }
    if (relativePath) {
        relativePath->clear();
    }

    const QString localPath = localPathFromInput(sourcePath.trimmed());
    if (localPath.isEmpty()) {
        return fail(QStringLiteral("未选择任何文件"));
    }

    const QFileInfo info(localPath);
    if (!info.exists() || !info.isFile()) {
        return fail(QStringLiteral("文件不存在：%1").arg(localPath));
    }

    const bool isFont = kind == AssetKind::Font;
    const bool isVideo = kind == AssetKind::Video;
    const QStringList allowed = isFont ? kFontExtensions : (isVideo ? kVideoExtensions : kImageExtensions);
    const qint64 limit = isFont ? kFontLimit : (isVideo ? kVideoLimit : kImageLimit);

    const QString extension = info.suffix().toLower();
    if (!allowed.contains(extension)) {
        return fail(QStringLiteral("不支持的%1格式：.%2")
                        .arg(kindLabel(isFont, isVideo), extension.isEmpty() ? QStringLiteral("(无)") : extension));
    }

    const qint64 size = info.size();
    if (size <= 0) {
        return fail(QStringLiteral("文件为空，无法导入"));
    }
    if (size > limit) {
        return fail(QStringLiteral("文件过大（%1 MB，上限 %2 MB）")
                        .arg(bytesToMbCeil(size))
                        .arg(limit / kBytesPerMb));
    }

    const QString dirPath = QDir(m_paths.assetsDir()).filePath(subDir);
    if (!QDir().mkpath(dirPath)) {
        return fail(QStringLiteral("无法创建资源目录：%1").arg(dirPath));
    }

    const QString target = uniqueTargetPath(dirPath, sanitizeBaseName(info.completeBaseName()), extension);
    if (!QFile::copy(localPath, target)) {
        return fail(QStringLiteral("资源复制失败，请检查文件是否被占用或权限不足"));
    }

    if (relativePath) {
        *relativePath = QStringLiteral("assets/%1/%2").arg(subDir, QFileInfo(target).fileName());
    }
    return true;
}

void ThemeManager::removeAsset(const QString &relativePath) const
{
    if (relativePath.isEmpty() || !PersonalizationStore::isSafeRelativePath(relativePath)) {
        return;
    }
    const QString absolute = absolutePath(relativePath);
    if (!absolute.isEmpty() && QFile::exists(absolute)) {
        QFile::remove(absolute); // 尽力而为：失败不影响设置变更
    }
}

bool ThemeManager::persist()
{
    if (!m_store) {
        setLastError(QStringLiteral("个性化配置存储未就绪，设置未能保存"));
        return false;
    }
    QString error;
    if (!m_store->save(m_config, &error)) {
        setLastError(error);
        return false;
    }
    setLastError(QString());
    return true;
}

void ThemeManager::applyFontFamily()
{
    QGuiApplication *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!gui) {
        return; // 无图形应用实例时跳过，样式由调用方（QML）自行决定
    }
    gui->setFont(m_config.fontFamily.isEmpty() ? m_defaultFont : QFont(m_config.fontFamily));
}

bool ThemeManager::ensureWritable()
{
    if (!available()) {
        setLastError(QStringLiteral("个性化配置不可用，设置未生效"));
        return false;
    }
    return true;
}

void ThemeManager::registerImportedFont()
{
    if (m_config.fontSource.isEmpty()) {
        return;
    }
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    QFontDatabase::addApplicationFont(absolutePath(m_config.fontSource));
}

QString ThemeManager::absolutePath(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return QString();
    }
    return QDir(m_paths.root()).filePath(relativePath);
}

QString ThemeManager::toSourceUrl(const QString &relativePath) const
{
    const QString absolute = absolutePath(relativePath);
    if (absolute.isEmpty()) {
        return QString();
    }
    return QUrl::fromLocalFile(absolute).toString();
}

void ThemeManager::emitAllChanged()
{
    emit fontFamilyChanged();
    emit backgroundChanged();
    emit avatarChanged();
    emit splashChanged();
}

void ThemeManager::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    emit availableChanged();
}

void ThemeManager::setLastError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}
