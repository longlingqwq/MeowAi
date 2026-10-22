find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# 测试目标默认 EXCLUDE_FROM_ALL：日常构建只编译主程序，避免每次等 21 个测试
# 可执行文件重新链接。需要时：
#   cmake --build build --config Debug --target meowai_tests     # 全部测试
#   cmake --build build --config Debug --target tst_xxx          # 单个测试
# 版本验收阶段再跑完整 ctest。
add_custom_target(meowai_tests)

# 每个用例一个可执行文件，统一链接被测静态库
# fakes/FakeNetworkTransport.h 含 Q_OBJECT，显式列出以触发 AUTOMOC
function(meowai_add_test name)
    qt_add_executable(${name} ${name}.cpp fakes/FakeNetworkTransport.h)
    set_target_properties(${name} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_link_libraries(${name} PRIVATE MeowAiLib Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(meowai_tests ${name})
    # 测试 exe 不在 MeowAi.exe 的部署目录里：把 Qt bin 加进测试进程 PATH，
    # 避免 Windows 弹出「找不到 Qt6xxx.dll」的阻塞弹窗。
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 120
    )
endfunction()

meowai_add_test(tst_storage)
meowai_add_test(tst_securestore)
meowai_add_test(tst_theme)
meowai_add_test(tst_provider)
meowai_add_test(tst_sse)
meowai_add_test(tst_skill)
meowai_add_test(tst_retriever)
meowai_add_test(tst_chatstore)
meowai_add_test(tst_themepreset)
meowai_add_test(tst_providercatalog)
meowai_add_test(tst_aiconfigstore)
meowai_add_test(tst_websearch)
meowai_add_test(tst_toolcall)
meowai_add_test(tst_librarystore)
meowai_add_test(tst_libraryconfig)
meowai_add_test(tst_librarymanager)
meowai_add_test(tst_extractors)
meowai_add_test(tst_imagecapture)
meowai_add_test(tst_chunk)
meowai_add_test(tst_embedding)
meowai_add_test(tst_ragretriever)
meowai_add_test(tst_cost)
meowai_add_test(tst_referencemode)
meowai_add_test(tst_libraryservices)
meowai_add_test(tst_chatfeatures)
meowai_add_test(tst_libraryurls)

# 解析器用例会真跑 PDF 文本层：pdfium.dll 是运行期动态库，需与测试可执行文件同目录
if(TARGET pdfium)
    add_custom_command(TARGET tst_extractors POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:tst_extractors>"
    )
endif()

# 厂商目录自检需要读取源码树中的 resources/providers.json：
# 测试目标不携带 qrc 资源（与主题预设测试同一策略），故以编译宏传入源码根目录。
target_compile_definitions(tst_providercatalog PRIVATE MEOWAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# 成本预估测试同样读取源码树里的 resources/pricing.json（不依赖 qrc）
target_compile_definitions(tst_cost PRIVATE MEOWAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# 对话能力用例需要读取源码树的 resources/providers.json（自定义厂商拉取模型）
target_compile_definitions(tst_chatfeatures PRIVATE MEOWAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# 资料库地址优先级用例同样需要 catalog 数据；Qt Gui 用于构造内置 8x8 PNG
target_compile_definitions(tst_libraryurls PRIVATE MEOWAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
