# tests: unit-тесты core (GoogleTest + мок HTTP-сервер на QTcpServer).
find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tgui_tests
    ../app/icons/icons.qrc
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
    test_torrentreplacement.cpp
    test_i18n.cpp
    test_optionsdialog.cpp
    test_profiles.cpp
    test_daemonoptions.cpp
    test_colsetup.cpp
    test_about.cpp
    tst_pathmap.cpp
    tst_geoip.cpp
    # Скриншот-тесты темы (последними: apply() меняет глобальный стиль).
    test_theme.cpp
)
target_include_directories(tgui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# tgui_app_lib отдаёт исходники приложения, зависимости и TGUI_VERSION через PUBLIC.
target_link_libraries(tgui_tests PRIVATE tgui_app_lib Qt6::Test GTest::gtest)
tgui_set_warnings(tgui_tests)

# W06: путь к скомпилированному .qm (проверка конвейера переводов без «железного» русского).
target_compile_definitions(tgui_tests PRIVATE
    TGUI_QM_PATH="${PROJECT_BINARY_DIR}/app/translations/tgui_ru.qm")

add_dependencies(tgui_tests tgui_translations)

add_test(NAME tgui_tests COMMAND tgui_tests)
set_tests_properties(tgui_tests PROPERTIES TIMEOUT 180)
add_test(NAME tgui_app_smoke COMMAND transmissiongui --smoke-test)
set_tests_properties(tgui_app_smoke PROPERTIES TIMEOUT 15)

