cmake_minimum_required(VERSION 3.21)

project(MeowAi
    VERSION 0.2.1
    DESCRIPTION "MeowAi - 本地优先的 AI 助理客户端"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Network Sql Test Multimedia QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

# 应用 QML 统一放在 ui/ 子目录，模块根为工程根目录；此处沿用旧策略，
# 不强制为每个子目录生成 qmldir（本阶段 QML 仅一个子目录，无多模块需求）。
qt_policy(SET QTP0004 OLD)

option(MEOWAI_BUILD_TESTS "构建单元测试" ON)

# ---------------------------------------------------------------------------
# 分层静态库
# 依赖方向（禁止反向）：ui(QML) -> core -> {providers, storage} -> platform
# ---------------------------------------------------------------------------
add_library(MeowAiLib STATIC
    core/AppController.cpp
    core/chat/ChatController.cpp
    core/chat/ChatHistoryModel.cpp
    core/library/LibraryManager.cpp
    core/library/CategoryManager.cpp
    core/library/BackupManager.cpp
    core/retriever/NullRetriever.cpp
    core/skill/SkillRegistry.cpp
    core/theme/ThemeManager.cpp
    core/theme/ThemePresetManager.cpp
    core/update/UpdateManager.cpp

    providers/ProviderFactory.cpp
    providers/AiProvider.cpp
    providers/OpenAiProvider.cpp
    providers/AnthropicProvider.cpp
    providers/SseStreamParser.cpp
    providers/NetworkTransport.cpp
    providers/ProviderUtils.cpp
    providers/AiProvider.h        # 仅接口的头文件，需显式列出以触发 AUTOMOC
    providers/NetworkTransport.h  # 含 Q_OBJECT，需显式列出以触发 AUTOMOC

    storage/AppPaths.cpp
    storage/ChatStore.cpp
    storage/Database.cpp
    storage/PersonalizationStore.cpp
    storage/SecureStore.cpp
    storage/ThemeStore.cpp

    platform/PlatformAdapterFactory.cpp
    platform/impl/DesktopPlatformAdapter.cpp
    platform/impl/AndroidPlatformAdapter.cpp
    platform/impl/WebPlatformAdapter.cpp
    platform/impl/TitleBarHelper.cpp
)

target_include_directories(MeowAiLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MeowAiLib
    PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql
)

if(WIN32)
    # DPAPI（CryptProtectData / CryptUnprotectData）：API Key 本地加密（功能 #5）
    target_link_libraries(MeowAiLib PRIVATE crypt32)
    # DWM（DwmSetWindowAttribute）：无边框标题栏关闭系统绘制边框（阶段三）
    target_link_libraries(MeowAiLib PRIVATE dwmapi)
endif()

# ---------------------------------------------------------------------------
# 可执行程序：最小可运行 QML 主窗口
# ---------------------------------------------------------------------------
qt_add_executable(MeowAi main.cpp)

# 阶段三修复：GUI 子系统。Windows 下不再弹出控制台窗口，也不会出现
# 「关掉命令行 demo 才结束」的现象；--smoke-test 仍以退出码供 CI 判定。
set_target_properties(MeowAi PROPERTIES WIN32_EXECUTABLE TRUE)

# 设计令牌 Theme.qml 是 QML 单例：必须显式声明为 singleton，
# 否则生成的 qmldir 会把它当成普通类型，Theme.<token> 全部解析为 undefined。
set_source_files_properties(ui/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(MeowAi
    URI MeowAi
    VERSION 1.0
    QML_FILES
        ui/main.qml
        ui/Theme.qml
        # 组件库（平铺于 ui/，依赖同目录隐式导入，避免子目录 qmldir 风险）
        ui/MSurface.qml
        ui/MLabel.qml
        ui/MButton.qml
        ui/MField.qml
        ui/MCombo.qml
        ui/MSwitch.qml
        ui/MChip.qml
        ui/MGlow.qml
        ui/EyeMark.qml
        ui/MAvatar.qml
        ui/TitleBar.qml
        # 页面
        ui/ConversationPage.qml
        ui/PersonalizationPage.qml
        ui/SplashVideo.qml
)

# 内置 Skill 资源（功能 #9)：运行期路径为 ":/skills/<id>/manifest.json"
qt_add_resources(MeowAi "skills"
    PREFIX "/skills"
    BASE resources/skills
    FILES
        resources/skills/example/manifest.json
        resources/skills/example/skill.md
)

# 主题预设（阶段三）：运行期路径为 ":/themes/theme1.json"。
# core/ThemePresetManager 只按路径读取，新增主题＝新增一个 JSON 文件，零代码。
qt_add_resources(MeowAi "themes"
    PREFIX "/themes"
    BASE resources/themes
    FILES
        resources/themes/theme1.json
        resources/themes/theme2.json
)

# 可执行程序是 QML 宿主：Multimedia 仅用于 QML 侧开屏视频播放（功能 #11），
# QuickControls2 仅用于在 main.cpp 里锁定 Basic 样式（全自绘 UI 的前提），
# 两者都不进入 core 层，保持分层不被污染。
target_link_libraries(MeowAi PRIVATE MeowAiLib Qt6::Quick Qt6::Multimedia Qt6::QuickControls2)

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
if(MEOWAI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
