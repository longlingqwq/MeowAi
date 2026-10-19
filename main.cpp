#include "core/AppController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("MeowAi"));
    QGuiApplication::setApplicationName(QStringLiteral("MeowAi"));

    AppController controller;
    controller.initialize();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
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
