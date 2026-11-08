# tests: unit-тесты core (GoogleTest + мок HTTP-сервер на QTcpServer).
find_package(GTest REQUIRED)

add_executable(tgui_tests
    testmain.cpp
    mockhttpserver.cpp
    test_util.cpp
    test_bencode.cpp
    test_jsonrpc.cpp
    test_models.cpp
    test_rpcclient.cpp
    # GUI-модели списка (regress: пустая таблица после подключения).
    ${CMAKE_SOURCE_DIR}/app/src/torrentmodel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/groupedproxy.cpp
)
target_include_directories(tgui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(tgui_tests PRIVATE tgui_core Qt6::Widgets Qt6::Network GTest::gtest)
tgui_set_warnings(tgui_tests)

add_test(NAME tgui_tests COMMAND tgui_tests)
set_tests_properties(tgui_tests PROPERTIES TIMEOUT 180)