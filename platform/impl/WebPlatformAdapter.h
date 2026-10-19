#pragma once

#include "platform/PlatformAdapter.h"

/**
 * Web（WASM，承载 iOS 用户）平台适配器（占位）。
 * Web 端不支持覆盖升级与原生密钥库，相关能力直接返回“不支持”。
 */
class WebPlatformAdapter : public PlatformAdapter
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
