#include "core/theme/ThemeManager.h"

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

QString ThemeManager::fontFamily() const
{
    return m_fontFamily;
}

void ThemeManager::setFontFamily(const QString &fontFamily)
{
    if (m_fontFamily == fontFamily) {
        return;
    }
    m_fontFamily = fontFamily;
    emit fontFamilyChanged();
}

QString ThemeManager::backgroundPath() const
{
    return m_backgroundPath;
}

void ThemeManager::setBackgroundPath(const QString &path)
{
    if (m_backgroundPath == path) {
        return;
    }
    m_backgroundPath = path;
    emit backgroundPathChanged();
}

QString ThemeManager::avatarPath() const
{
    return m_avatarPath;
}

void ThemeManager::setAvatarPath(const QString &path)
{
    if (m_avatarPath == path) {
        return;
    }
    m_avatarPath = path;
    emit avatarPathChanged();
}

QString ThemeManager::splashAnimationPath() const
{
    return m_splashAnimationPath;
}

void ThemeManager::setSplashAnimationPath(const QString &path)
{
    if (m_splashAnimationPath == path) {
        return;
    }
    m_splashAnimationPath = path;
    emit splashAnimationPathChanged();
}
