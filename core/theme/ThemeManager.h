#pragma once

#include <QObject>
#include <QString>

/**
 * 全局主题管理器骨架（功能 #11）。
 *
 * 统一控制字体、背景、头像、开屏动画等个性化资源，是 UI 层唯一的样式来源。
 * 阶段一只暴露属性与信号，持久化（写入 config.json / assets）在阶段二实现。
 */
class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(QString backgroundPath READ backgroundPath WRITE setBackgroundPath NOTIFY backgroundPathChanged)
    Q_PROPERTY(QString avatarPath READ avatarPath WRITE setAvatarPath NOTIFY avatarPathChanged)
    Q_PROPERTY(QString splashAnimationPath READ splashAnimationPath WRITE setSplashAnimationPath NOTIFY splashAnimationPathChanged)

public:
    explicit ThemeManager(QObject *parent = nullptr);

    QString fontFamily() const;
    void setFontFamily(const QString &fontFamily);

    QString backgroundPath() const;
    void setBackgroundPath(const QString &path);

    QString avatarPath() const;
    void setAvatarPath(const QString &path);

    QString splashAnimationPath() const;
    void setSplashAnimationPath(const QString &path);

signals:
    void fontFamilyChanged();
    void backgroundPathChanged();
    void avatarPathChanged();
    void splashAnimationPathChanged();

private:
    QString m_fontFamily;       ///< 空串表示使用系统默认字体
    QString m_backgroundPath;
    QString m_avatarPath;
    QString m_splashAnimationPath;
};
