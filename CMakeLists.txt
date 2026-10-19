cmake_minimum_required(VERSION 3.21)

project(MeowAi
    VERSION 0.1.0
    DESCRIPTION "MeowAi - 本地优先的 AI 助理客户端"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Network Sql Test)

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
    core/library/LibraryManager.cpp
    core/library/CategoryManager.cpp
    core/library/BackupManager.cpp
    core/retriever/NullRetriever.cpp
    core/skill/SkillRegistry.cpp
    core/theme/ThemeManager.cpp
    core/update/UpdateManager.cpp

    providers/ProviderFactory.cpp
    providers/OpenAiProvider.cpp
    providers/AnthropicProvider.cpp
    providers/AiProvider.h  # 仅接口的头文件，需显式列出以触发 AUTOMOC

    storage/AppPaths.cpp
    storage/Database.cpp
    storage/SecureStore.cpp

    platform/PlatformAdapterFactory.cpp
    platform/impl/DesktopPlatformAdapter.cpp
    platform/impl/AndroidPlatformAdapter.cpp
    platform/impl/WebPlatformAdapter.cpp
)

target_include_directories(MeowAiLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MeowAiLib
    PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql
)

# ---------------------------------------------------------------------------
# 可执行程序：最小可运行 QML 主窗口
# ---------------------------------------------------------------------------
qt_add_executable(MeowAi main.cpp)

qt_add_qml_module(MeowAi
    URI MeowAi
    VERSION 1.0
    QML_FILES
        ui/main.qml
        ui/Theme.qml
)

# 内置 Skill 资源（功能 #9）：运行期路径为 ":/skills/<id>/manifest.json"
qt_add_resources(MeowAi "skills"
    PREFIX "/skills"
    BASE resources/skills
    FILES
        resources/skills/example/manifest.json
        resources/skills/example/skill.md
)

target_link_libraries(MeowAi PRIVATE MeowAiLib Qt6::Quick)

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
if(MEOWAI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
