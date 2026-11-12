# app: графическое приложение (Qt 6 Widgets).
add_executable(transmissiongui WIN32
    src/main.cpp
    src/externalopen.cpp
    src/theme.cpp
    src/mainwindow.cpp
    src/torrentmodel.cpp
    src/addlinkdialog.cpp
    src/connectdialog.cpp
    src/fileselectdialog.cpp
    src/groupedproxy.cpp
    src/delegates.cpp
    src/filterpanel.cpp
    src/detailmodels.cpp
    src/propsform.cpp
    src/statsform.cpp
    src/propsdialog.cpp
    src/movedialog.cpp
    src/optionsdialog.cpp
    src/daemonprofile.cpp
    src/connoptionsdialog.cpp
    src/daemonoptionsdialog.cpp
    src/colsetup.cpp
    src/aboutdialog.cpp
    # Явное перечисление Q_OBJECT-заголовков для AUTOMOC (как в core).
    src/mainwindow.h
    src/theme.h
    src/torrentmodel.h
    src/addlinkdialog.h
    src/connectdialog.h
    src/fileselectdialog.h
    src/groupedproxy.h
    src/delegates.h
    src/filterpanel.h
    src/detailmodels.h
    src/propsform.h
    src/statsform.h
    src/propsdialog.h
    src/movedialog.h
    src/optionsdialog.h
    src/daemonprofile.h
    src/connoptionsdialog.h
    src/daemonoptionsdialog.h
    src/colsetup.h
    src/aboutdialog.h
)
target_include_directories(transmissiongui PRIVATE src)
target_compile_definitions(transmissiongui PRIVATE TGUI_VERSION="${PROJECT_VERSION}")
target_link_libraries(transmissiongui PRIVATE tgui_core Qt6::Widgets Qt6::Network ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(transmissiongui PRIVATE shell32)
endif()
tgui_set_warnings(transmissiongui)

# i18n (W06): компилируем tgui_ru.ts -> tgui_ru.qm рядом с exe (<build>/app/translations/).
# Переводчик загружается в main.cpp; по умолчанию русский, fallback на английский, если .qm нет.
set(TGUI_I18N_TS ${CMAKE_SOURCE_DIR}/translations/tgui_ru.ts)
set(TGUI_I18N_QM ${CMAKE_CURRENT_BINARY_DIR}/translations/tgui_ru.qm)
add_custom_command(
    OUTPUT ${TGUI_I18N_QM}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/translations
    COMMAND Qt6::lrelease ${TGUI_I18N_TS} -qm ${TGUI_I18N_QM}
    DEPENDS ${TGUI_I18N_TS}
    COMMENT "Compiling translation tgui_ru.ts -> tgui_ru.qm"
    VERBATIM)
add_custom_target(tgui_translations DEPENDS ${TGUI_I18N_QM})
add_dependencies(transmissiongui tgui_translations)
qt_add_resources(transmissiongui "translations"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES "${TGUI_I18N_QM}")

if(APPLE)
    set_target_properties(transmissiongui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.transmissiongui.client"
        MACOSX_BUNDLE_BUNDLE_NAME "transmissionGUI"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()
install(TARGETS transmissiongui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(APPLE)
    qt_generate_deploy_app_script(TARGET transmissiongui OUTPUT_SCRIPT deploy_script)
    install(SCRIPT "${deploy_script}")
elseif(UNIX)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/transmissiongui.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
