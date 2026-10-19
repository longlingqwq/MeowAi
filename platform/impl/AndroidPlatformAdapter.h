#pragma once

#include "platform/PlatformAdapter.h"

/**
 * Android 平台适配器（占位）。
 * 与桌面端的差异集中在安装器（覆盖安装 Apk）与数据目录，后续在此处接 JNI。
 */
class AndroidPlatformAdapter : public PlatformAdapter
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
