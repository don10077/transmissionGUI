# tests: unit-тесты core (GoogleTest + мок HTTP-сервер на QTcpServer).
find_package(GTest REQUIRED)

add_executable(tgui_tests
    testmain.cpp
    mockhttpserver.cpp
    test_util.cpp
    test_bencode.cpp
    test_jsonrpc.cpp
    test_models.cpp
    test_detailmodels.cpp
    test_panels.cpp
    test_e2e_tabs.cpp
    test_rpcclient.cpp
    test_addlinkdialog.cpp
    # GUI-модели списка (regress: пустая таблица после подключения).
    ${CMAKE_SOURCE_DIR}/app/src/torrentmodel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/groupedproxy.cpp
    ${CMAKE_SOURCE_DIR}/app/src/delegates.cpp
    ${CMAKE_SOURCE_DIR}/app/src/filterpanel.cpp
    ${CMAKE_SOURCE_DIR}/app/src/detailmodels.cpp
    # MainWindow + диалоги — нужны для E2E-теста вкладок.
    ${CMAKE_SOURCE_DIR}/app/src/theme.cpp
    ${CMAKE_SOURCE_DIR}/app/src/mainwindow.cpp
    ${CMAKE_SOURCE_DIR}/app/src/propsform.cpp
    ${CMAKE_SOURCE_DIR}/app/src/statsform.cpp
    ${CMAKE_SOURCE_DIR}/app/src/adddialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/addlinkdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/connectdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/fileselectdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/propsdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/movedialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/mainwindow.h
    ${CMAKE_SOURCE_DIR}/app/src/theme.h
    ${CMAKE_SOURCE_DIR}/app/src/adddialog.h
    ${CMAKE_SOURCE_DIR}/app/src/addlinkdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/connectdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/fileselectdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/propsdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/propsform.h
    ${CMAKE_SOURCE_DIR}/app/src/statsform.h
    ${CMAKE_SOURCE_DIR}/app/src/delegates.h
    ${CMAKE_SOURCE_DIR}/app/src/filterpanel.h
    ${CMAKE_SOURCE_DIR}/app/src/movedialog.h
)
target_include_directories(tgui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(tgui_tests PRIVATE tgui_core Qt6::Widgets Qt6::Network GTest::gtest)
tgui_set_warnings(tgui_tests)

add_test(NAME tgui_tests COMMAND tgui_tests)
set_tests_properties(tgui_tests PROPERTIES TIMEOUT 180)