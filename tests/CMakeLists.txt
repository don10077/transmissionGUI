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
    test_fileselectdialog.cpp
    test_i18n.cpp
    test_optionsdialog.cpp
    test_profiles.cpp
    test_daemonoptions.cpp
    test_colsetup.cpp
    test_about.cpp
    tst_pathmap.cpp
    tst_geoip.cpp
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
    ${CMAKE_SOURCE_DIR}/app/src/addlinkdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/connectdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/fileselectdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/propsdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/movedialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/optionsdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/daemonprofile.cpp
    ${CMAKE_SOURCE_DIR}/app/src/connoptionsdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/daemonoptionsdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/colsetup.cpp
    ${CMAKE_SOURCE_DIR}/app/src/aboutdialog.cpp
    ${CMAKE_SOURCE_DIR}/app/src/mainwindow.h
    ${CMAKE_SOURCE_DIR}/app/src/theme.h
    ${CMAKE_SOURCE_DIR}/app/src/addlinkdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/connectdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/fileselectdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/propsdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/propsform.h
    ${CMAKE_SOURCE_DIR}/app/src/statsform.h
    ${CMAKE_SOURCE_DIR}/app/src/optionsdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/daemonprofile.h
    ${CMAKE_SOURCE_DIR}/app/src/connoptionsdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/daemonoptionsdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/colsetup.h
    ${CMAKE_SOURCE_DIR}/app/src/aboutdialog.h
    ${CMAKE_SOURCE_DIR}/app/src/delegates.h
    ${CMAKE_SOURCE_DIR}/app/src/filterpanel.h
    ${CMAKE_SOURCE_DIR}/app/src/movedialog.h
)
target_include_directories(tgui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(tgui_tests PRIVATE tgui_core Qt6::Widgets Qt6::Network GTest::gtest z)
tgui_set_warnings(tgui_tests)

# W06: путь к скомпилированному .qm (проверка конвейера переводов без «железного» русского).
target_compile_definitions(tgui_tests PRIVATE
    TGUI_QM_PATH="${CMAKE_BINARY_DIR}/app/translations/tgui_ru.qm")
add_dependencies(tgui_tests tgui_translations)

add_test(NAME tgui_tests COMMAND tgui_tests)
set_tests_properties(tgui_tests PROPERTIES TIMEOUT 180)
