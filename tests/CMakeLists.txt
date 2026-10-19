find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# 每个用例一个可执行文件，统一链接被测静态库
# fakes/FakeNetworkTransport.h 含 Q_OBJECT，显式列出以触发 AUTOMOC
function(meowai_add_test name)
    qt_add_executable(${name} ${name}.cpp fakes/FakeNetworkTransport.h)
    target_link_libraries(${name} PRIVATE MeowAiLib Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

meowai_add_test(tst_storage)
meowai_add_test(tst_securestore)
meowai_add_test(tst_theme)
meowai_add_test(tst_provider)
meowai_add_test(tst_sse)
meowai_add_test(tst_skill)
meowai_add_test(tst_retriever)
