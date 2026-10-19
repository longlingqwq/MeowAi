#pragma once

#include <QObject>
#include <QString>

/**
 * 更新管理器骨架（功能 #2）。
 * 阶段一只固定信号契约；版本检测、蓝奏云下载解析与覆盖升级安装（经平台层）在阶段三实现。
 * Web 端不支持自更新（承载 iOS 用户，见 docs/PROJECT_CHARTER.md D4）。
 */
class UpdateManager : public QObject
{
    Q_OBJECT

public:
    explicit UpdateManager(QObject *parent = nullptr);

    /// 检查更新；阶段一未实现，始终以 checkFailed 结束
    Q_INVOKABLE void checkForUpdates();

signals:
    void updateAvailable(const QString &version, const QString &url);
    void checkFailed(const QString &message);
};
