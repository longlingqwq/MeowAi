#include "core/AppController.h"
#include "core/chat/ChatController.h"
#include "core/chat/ChatHistoryModel.h"
#include "core/library/LibraryController.h"
#include "core/theme/ThemeManager.h"
#include "core/theme/ThemePresetManager.h"
#include "platform/TitleBarHelper.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <QWindow>

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
    // 会话历史（阶段三）：UI 只绑定该模型，不直连数据库
    engine.rootContext()->setContextProperty(QStringLiteral("History"), controller.chatHistoryModel());
    // 主题预设（阶段三）：与 QML 单例 Theme 区分，承载“换肤 + 持久化”职责
    engine.rootContext()->setContextProperty(QStringLiteral("ThemePreset"),
                                             controller.themePresetManager());
    // 资料库（v0.2.5）：小库 / 文件 / 索引 / 检索配置 / 成本预估的唯一入口
    engine.rootContext()->setContextProperty(QStringLiteral("Library"),
                                             controller.libraryController());
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/MeowAi/ui/main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    // 主题令牌写到 QML 单例 Theme 对象上：普通属性赋值会触发变更通知，界面绑定即时刷新。
    // 界面加载完成后再应用，保证 Theme.qml 的默认值先就位，随后被所选预设覆盖。
    QObject *themeSingleton = engine.singletonInstance<QObject *>(QStringLiteral("MeowAi"),
                                                                  QStringLiteral("Theme"));
    if (ThemePresetManager *presets = controller.themePresetManager()) {
        presets->attachThemeObject(themeSingleton);
    }

    // 无边框标题栏：Windows 安装原生缩放边框助手（WM_NCHITTEST）；
    // 其它桌面平台返回 nullptr，由 QML 侧 startSystemResize 兜底。
    if (controller.customTitleBar()) {
        if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().value(0))) {
            MeowAiPlatform::installTitleBarHelper(window, &app);
        }
    }

    // 冒烟自检：仅在显式传入 --smoke-test 时加载后立即退出，用于 CI 与本地验证“可启动”
    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
