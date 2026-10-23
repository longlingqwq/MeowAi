cmake_minimum_required(VERSION 3.21)

project(MeowAi
    VERSION 0.2.7.1
    DESCRIPTION "MeowAi - 本地优先的 AI 助理客户端"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Network Sql Test Multimedia QuickControls2 Svg)

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
    core/ai/ProviderCatalog.cpp
    core/chat/ChatController.cpp
    core/chat/ChatHistoryModel.cpp
    core/library/CollectionManager.cpp
    core/library/CostEstimator.cpp
    core/library/LibraryController.cpp
    core/library/LibraryFileKinds.cpp
    core/library/LibraryIndexer.cpp
    core/library/DocumentMeta.cpp
    core/library/LibraryManager.cpp
    core/library/CategoryManager.cpp
    core/library/BackupManager.cpp
    core/library/ImageCaptionService.cpp
    core/library/extract/CsvExtractor.cpp
    core/library/extract/DocxExtractor.cpp
    core/library/extract/ExtractorRegistry.cpp
    core/library/extract/ImageCaptionExtractor.cpp
    core/library/extract/ImageTranscoder.cpp
    core/library/extract/JsonExtractor.cpp
    core/library/extract/OfficeXmlUtils.cpp
    core/library/extract/PdfTextExtractor.cpp
    core/library/extract/PlainTextExtractor.cpp
    core/library/extract/PptxExtractor.cpp
    core/library/extract/VideoExtractor.cpp
    core/library/extract/XlsxExtractor.cpp
    core/retriever/BudgetCalculator.cpp
    core/retriever/ChunkSplitter.cpp
    core/retriever/EmbeddingService.cpp
    core/retriever/FullTextAssembler.cpp
    core/retriever/RagRetriever.cpp
    core/retriever/NullRetriever.cpp
    core/retriever/SectionPicker.cpp
    core/retriever/InjectionPlanner.cpp
    core/retriever/FileLevelRetriever.cpp
    core/skill/SkillRegistry.cpp
    core/theme/ThemeManager.cpp
    core/theme/ThemePresetManager.cpp
    core/update/UpdateManager.cpp
    core/web/HttpWebSearchProvider.cpp
    core/web/WebSearchService.cpp
    core/web/WebSearchTool.cpp

    providers/ProviderFactory.cpp
    providers/AiProvider.cpp
    providers/OpenAiProvider.cpp
    providers/AnthropicProvider.cpp
    providers/SseStreamParser.cpp
    providers/NetworkTransport.cpp
    providers/ProviderUtils.cpp
    providers/AiProvider.h        # 仅接口的头文件，需显式列出以触发 AUTOMOC
    providers/NetworkTransport.h  # 含 Q_OBJECT，需显式列出以触发 AUTOMOC
    core/ai/ProviderCatalog.h     # 含 Q_OBJECT，需显式列出以触发 AUTOMOC
    core/web/WebSearchProvider.h   # 含 Q_OBJECT，需显式列出以触发 AUTOMOC
    core/web/WebSearchTool.h       # 含 Q_OBJECT，需显式列出以触发 AUTOMOC

    storage/AppPaths.cpp
    storage/AiConfigStore.cpp
    storage/ChatStore.cpp
    storage/Database.cpp
    storage/DocumentStore.cpp
    storage/LibraryConfigStore.cpp
    storage/LibraryStore.cpp
    storage/PersonalizationStore.cpp
    storage/SecureStore.cpp
    storage/SqliteVectorIndex.cpp
    storage/ThemeStore.cpp
    storage/WebConfigStore.cpp

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

# exe 图标（v0.2.6 交付）：资源管理器 / 任务栏 / 快捷方式读取 exe 内嵌图标，
# 既不依赖 DLL 也不需要额外文件；安装包图标由 scripts/MeowAi.iss 的 SetupIconFile 指定。
if(WIN32)
    target_sources(MeowAi PRIVATE resources/branding/MeowAi.rc)
endif()

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
        ui/MToast.qml
        ui/MGlow.qml
        ui/EyeMark.qml
        ui/MAvatar.qml
        ui/TitleBar.qml
        # 外壳组件（M1）：侧栏 / 模型条 / 会话列表容器 / 资料库配置面板 / 头像裁剪
        ui/Sidebar.qml
        ui/ModelStrip.qml
        ui/ConversationList.qml
        ui/LibraryConfigPanel.qml
        ui/AvatarCrop.qml
        ui/ModelMenu.qml
        ui/WebSearchPanel.qml
        # 页面
        ui/ConversationPage.qml
        ui/PersonalizationPage.qml
        ui/LibraryPage.qml
        ui/LibraryConsent.qml
        ui/SplashVideo.qml
        ui/SplashIntro.qml
        # 品牌标识：按主题选用琥珀 / 浅蓝官方 logo
        ui/BrandLogo.qml
)

# 内置 Skill 资源（功能 #9)：运行期路径为 ":/skills/<id>/manifest.json"
qt_add_resources(MeowAi "skills"
    PREFIX "/skills"
    BASE resources/skills
    FILES
        resources/skills/example/manifest.json
        resources/skills/example/skill.md
)

# 内置开屏资源（功能 #11）：两版共用一条离线合成的音轨，运行期路径 ":/splash/audio/intro.wav"。
qt_add_resources(MeowAi "splash"
    PREFIX "/splash"
    BASE resources/splash
    FILES
        resources/splash/audio/intro.wav
        # 官方 logo 拆分件：cat 去掉星点与文字（星点由动画自绘），word 仅文字
        resources/splash/amber/cat.svg
        resources/splash/amber/word.svg
        resources/splash/blue/cat.svg
        resources/splash/blue/word.svg
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

# 厂商 / 模型目录（v0.2.3 AI 接入增强）：运行期路径为 ":/providers/providers.json"。
# core/ai/ProviderCatalog 只按路径读取，新增厂商＝新增一个 JSON 条目，零代码。
qt_add_resources(MeowAi "providers"
    PREFIX "/providers"
    BASE resources
    FILES
        resources/providers.json
)

# 资料库成本参考价（v0.2.5）：运行期路径 ":/pricing/pricing.json"。
# core/library/CostEstimator 只按路径读取；价格更新＝改这个文件，零代码。
qt_add_resources(MeowAi "pricing"
    PREFIX "/pricing"
    BASE resources
    FILES
        resources/pricing.json
)

# 应用图标（v0.2.6 交付）：运行期路径 ":/branding/app.png"，供 main.cpp 设置窗口图标。
# 无边框窗口在任务栏/Alt+Tab 的图标取自 QWindow::icon，不设置会退回 Qt 默认图标。
qt_add_resources(MeowAi "branding"
    PREFIX "/branding"
    BASE resources/branding
    FILES
        resources/branding/app.png
        # 官方品牌 logo：logo 含猫 + 星点 + 文字，mark 仅猫 + 星点（紧凑位置）
        resources/branding/logo-amber.svg
        resources/branding/logo-blue.svg
        resources/branding/mark-amber.svg
        resources/branding/mark-blue.svg
)

# 可执行程序是 QML 宿主：Multimedia 仅用于 QML 侧开屏视频播放（功能 #11），
# QuickControls2 仅用于在 main.cpp 里锁定 Basic 样式（全自绘 UI 的前提），
# Svg 提供 Image 加载 .svg 的图片插件（品牌 logo 资源），
# 三者都不进入 core 层，保持分层不被污染。
target_link_libraries(MeowAi PRIVATE MeowAiLib Qt6::Quick Qt6::Multimedia Qt6::QuickControls2 Qt6::Svg)

# ---------------------------------------------------------------------------
# PDFium（可选依赖，模块 M3-c：PDF 文本层解析）
# 产物**本地放置、仓库忽略**（.gitignore 已忽略 third_party/pdfium/，不进 PR diff）。
# 抓取： pwsh scripts/fetch-pdfium.ps1   （固定 tag chromium/8111）
# 目录存在即链接并定义 MEOWAI_HAVE_PDFIUM；缺失则编译降级实现：
# 解析 pdf 时返回 false + 「PDF 支持未启用」，由上层落成 no_text_layer，不影响其它格式。
# ---------------------------------------------------------------------------
set(MEOWAI_PDFIUM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium")
if(EXISTS "${MEOWAI_PDFIUM_DIR}/include/fpdfview.h"
   AND EXISTS "${MEOWAI_PDFIUM_DIR}/include/fpdf_text.h"
   AND EXISTS "${MEOWAI_PDFIUM_DIR}/lib/pdfium.dll.lib")
    add_library(pdfium SHARED IMPORTED GLOBAL)
    set_target_properties(pdfium PROPERTIES
        IMPORTED_LOCATION "${MEOWAI_PDFIUM_DIR}/bin/pdfium.dll"
        IMPORTED_IMPLIB "${MEOWAI_PDFIUM_DIR}/lib/pdfium.dll.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${MEOWAI_PDFIUM_DIR}/include"
    )
    # PUBLIC：测试目标需要据此切换断言分支（有/无 PDFium 两种口径）
    target_link_libraries(MeowAiLib PRIVATE pdfium)
    target_compile_definitions(MeowAiLib PUBLIC MEOWAI_HAVE_PDFIUM=1)

    # Windows 运行期不查 PATH 之外的库路径：pdfium.dll 必须与可执行文件同目录
    add_custom_command(TARGET MeowAi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:MeowAi>"
    )
    message(STATUS "PDFium 已启用（PDF 文本层解析）：${MEOWAI_PDFIUM_DIR}")
else()
    message(STATUS "PDFium 未检测到：PDF 解析降级为「未启用」（抓取：pwsh scripts/fetch-pdfium.ps1）")
endif()

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
if(MEOWAI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
