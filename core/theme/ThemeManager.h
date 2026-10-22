#pragma once

#include "storage/AppPaths.h"
#include "storage/PersonalizationStore.h"

#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * 个性化配置中心（功能 #11）——全局主题管理器。
 *
 * 职责边界（**不与其他模块杂糅**）：
 *  - 只管个性化：字体、背景、头像、开屏视频（含音效）；
 *  - 配置读写全部委托 `storage/PersonalizationStore`，只改 `config.json` 的
 *    `personalization` 段，不触碰 API Key（Key 只存 SecureStore 保险库）；
 *  - 用户资源一律复制进 `assets/` 子目录后再引用，落盘的是相对路径，
 *    个性化资源属"用户文件类数据"，**绝不上传**；
 *  - 是 UI 层唯一的样式来源（见 docs/PROJECT_CHARTER.md §5），
 *    字体通过 QGuiApplication::setFont 全局生效。
 *
 * 防崩溃：任何失败（配置损坏、资源缺失、文件过大、路径穿越、依赖为空）
 * 都只写 lastError 并回退默认值，不抛异常、不崩溃。
 */
class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(QString backgroundSource READ backgroundSource NOTIFY backgroundChanged)
    Q_PROPERTY(QString avatarSource READ avatarSource NOTIFY avatarChanged)
    Q_PROPERTY(QString splashSource READ splashSource NOTIFY splashChanged)
    Q_PROPERTY(bool splashEnabled READ splashEnabled WRITE setSplashEnabled NOTIFY splashChanged)
    Q_PROPERTY(bool splashSoundEnabled READ splashSoundEnabled WRITE setSplashSoundEnabled NOTIFY splashChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    ThemeManager(AppPaths paths, PersonalizationStore *store, QObject *parent = nullptr);
    ~ThemeManager() override;

    QString fontFamily() const;
    /// 背景 / 头像 / 开屏的 file:// URL（空串表示使用默认外观）
    QString backgroundSource() const;
    QString avatarSource() const;
    QString splashSource() const;
    /// 是否在启动时播放开屏（内置开屏默认开启）
    bool splashEnabled() const;
    void setSplashEnabled(bool enabled);
    bool splashSoundEnabled() const;
    void setSplashSoundEnabled(bool enabled);
    /// 个性化配置是否可用（依赖未就绪时为 false）
    bool available() const;
    QString lastError() const;

    /// 启动时载入 config.json；缺失或损坏都回退默认值，只体现在 lastError
    Q_INVOKABLE void load();

    /// 系统已安装字体族列表（供「系统字体」选择）
    Q_INVOKABLE QStringList availableFontFamilies() const;

    /// 选择系统字体；传空串表示恢复默认字体
    Q_INVOKABLE bool setSystemFont(const QString &family);
    /// 导入字体文件（复制进 assets/fonts 并注册）
    Q_INVOKABLE bool importFont(const QString &sourcePath);

    Q_INVOKABLE bool setBackground(const QString &sourcePath);
    Q_INVOKABLE bool setAvatar(const QString &sourcePath);
    /// 设置开屏视频（含音轨），成功后自动开启开屏播放
    Q_INVOKABLE bool setSplash(const QString &sourcePath);

    Q_INVOKABLE bool clearFont();
    Q_INVOKABLE bool clearBackground();
    Q_INVOKABLE bool clearAvatar();
    /// 清除自定义开屏视频；开屏开关保持不变，splashSource 为空时回落到内置开屏
    Q_INVOKABLE bool clearSplash();
    /// 恢复全部默认外观，并清理已导入的资源文件
    Q_INVOKABLE bool resetAll();

signals:
    void fontFamilyChanged();
    void backgroundChanged();
    void avatarChanged();
    void splashChanged();
    void availableChanged();
    void lastErrorChanged();

private:
    enum class AssetKind { Font, Image, Video };

    /// 复制用户资源进 assets/<subDir>；成功时输出相对路径，失败写 error
    bool importAsset(const QString &sourcePath, AssetKind kind, const QString &subDir,
                     QString *relativePath, QString *error) const;
    /// 删除本模块导入的资源（尽力而为，失败忽略）
    void removeAsset(const QString &relativePath) const;

    bool persist();
    /// 配置不可用（依赖未就绪 / 未 load）时拒绝任何写入
    bool ensureWritable();
    void applyFontFamily();
    void registerImportedFont();
    QString absolutePath(const QString &relativePath) const;
    QString toSourceUrl(const QString &relativePath) const;
    void emitAllChanged();
    void setAvailable(bool available);
    void setLastError(const QString &message);

    AppPaths m_paths;
    PersonalizationStore *m_store = nullptr;
    PersonalizationConfig m_config;
    QFont m_defaultFont;
    QString m_lastError;
    bool m_available = false;
};
