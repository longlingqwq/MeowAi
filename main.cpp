#include "core/AppController.h"
#include "core/chat/ChatController.h"
#include "core/theme/ThemeManager.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>

int main(int argc, char *argv[])
{
    // 强制 Basic 样式：Windows 默认的 native（Windows11）样式会拒绝 QML 侧的
    // background / contentItem / indicator 定制，与本项目的完全自绘 UI 冲突。
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("MeowAi"));
    QGuiApplication::setApplicationName(QStringLiteral("MeowAi"));

    AppController controller;
    controller.initialize();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("Chat"), controller.chatController());
    // 个性化（功能 #11）：刻意命名为 Personalize，避免与 QML 单例 Theme 冲突
    engine.rootContext()->setContextProperty(QStringLiteral("Personalize"), controller.themeManager());
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/MeowAi/ui/main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    // 冒烟自检：仅在显式传入 --smoke-test 时加载后立即退出，用于 CI 与本地验证“可启动”
    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
