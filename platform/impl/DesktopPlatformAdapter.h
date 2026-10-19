#pragma once

#include "platform/PlatformAdapter.h"

/**
 * 桌面端（Windows / Linux / macOS）平台适配器。
 */
class DesktopPlatformAdapter : public PlatformAdapter
{
public:
    QString platformId() const override;
    QString appDataRoot() const override;
    QString cacheRoot() const override;
    QString bundledSkillsRoot() const override;
    bool isNetworkAvailable() const override;

    bool writeSecret(const QString &key, const QString &value, QString *error) override;
    bool readSecret(const QString &key, QString *value, QString *error) override;
    bool removeSecret(const QString &key, QString *error) override;

    bool supportsSelfUpdate() const override;
    bool installUpdate(const QString &packagePath, QString *error) override;
};
