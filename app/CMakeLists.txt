# app: графическое приложение (Qt 6 Widgets).
#
# Вся логика UI вынесена в статическую библиотеку tgui_app_lib: её же
# переиспользуют тесты, поэтому исходники приложения не компилируются дважды.
add_library(tgui_app_lib STATIC
    src/externalopen.cpp
    src/theme.cpp
    src/connectbutton.cpp
    src/watchfolderservice.cpp
    src/mainwindow.cpp
    src/torrentmodel.cpp
    src/addlinkdialog.cpp
    src/connectdialog.cpp
    src/fileselectdialog.cpp
    src/torrentreplacement.cpp
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
    src/secretstore.cpp
    src/connoptionsdialog.cpp
    src/daemonoptionsdialog.cpp
    src/colsetup.cpp
    src/aboutdialog.cpp
    src/startup.cpp
    src/assoc.cpp
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
target_include_directories(tgui_app_lib PUBLIC src)
target_compile_definitions(tgui_app_lib PUBLIC
    TGUI_VERSION="${PROJECT_VERSION}"
    TGUI_DISPLAY_NAME="TransmissionGUI")
target_link_libraries(tgui_app_lib PUBLIC tgui_core Qt6::Widgets Qt6::Network Qt6::Svg
    Qt6::Concurrent ZLIB::ZLIB LibArchive::LibArchive)
if(WIN32)
    target_link_libraries(tgui_app_lib PUBLIC shell32 crypt32)
endif()
tgui_set_warnings(tgui_app_lib)

# Исполняемый файл: только точка входа, ресурсы и иконка (RC-файл).
add_executable(transmissiongui WIN32
    src/main.cpp
    icons/icons.qrc
)
target_link_libraries(transmissiongui PRIVATE tgui_app_lib)
if(WIN32)
    enable_language(RC)
    set_target_properties(transmissiongui PROPERTIES OUTPUT_NAME "TransmissionGUI")
    configure_file(icons/transmissiongui.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/transmissiongui.rc" @ONLY)
    target_sources(transmissiongui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/transmissiongui.rc")
    target_include_directories(transmissiongui PRIVATE
        "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/icons>")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/transmissiongui.rc" APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/icons/transmission-upstream.ico")
endif()
tgui_set_warnings(transmissiongui)

# Передаём lupdate только исходники: Qt 6.4 ещё не умеет искать строки
# в нескольких целях через qt_add_translations, а у exe есть настройки для RC.
set(TGUI_I18N_TS "${PROJECT_SOURCE_DIR}/translations/tgui_ru.ts")
set(TGUI_I18N_QM "${CMAKE_CURRENT_BINARY_DIR}/translations/tgui_ru.qm")
get_target_property(tgui_translation_sources tgui_app_lib SOURCES)
add_custom_target(update_translations
    COMMAND Qt6::lupdate -no-obsolete ${tgui_translation_sources} src/main.cpp -ts "${TGUI_I18N_TS}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_command(
    OUTPUT "${TGUI_I18N_QM}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/translations"
    COMMAND Qt6::lrelease "${TGUI_I18N_TS}" -qm "${TGUI_I18N_QM}"
    DEPENDS "${TGUI_I18N_TS}"
    VERBATIM)
add_custom_target(release_translations DEPENDS "${TGUI_I18N_QM}")
add_dependencies(transmissiongui release_translations)
qt_add_resources(transmissiongui translations
    PREFIX "/"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES "${TGUI_I18N_QM}")

if(APPLE)
    # .icns генерируется из PNG на этапе сборки: бинарный файл не хранится в репозитории.
    set(_tgui_icon_png "${CMAKE_CURRENT_SOURCE_DIR}/icons/transmission-upstream.png")
    set(_tgui_iconset "${CMAKE_CURRENT_BINARY_DIR}/transmissiongui.iconset")
    set(_tgui_icns "${CMAKE_CURRENT_BINARY_DIR}/transmissiongui.icns")
    add_custom_command(
        OUTPUT "${_tgui_icns}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_tgui_iconset}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_tgui_iconset}"
        COMMAND sips -z 16 16 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_16x16.png"
        COMMAND sips -z 32 32 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_16x16@2x.png"
        COMMAND sips -z 32 32 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_32x32.png"
        COMMAND sips -z 64 64 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_32x32@2x.png"
        COMMAND sips -z 128 128 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_128x128.png"
        COMMAND sips -z 256 256 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_128x128@2x.png"
        COMMAND sips -z 256 256 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_256x256.png"
        COMMAND sips -z 512 512 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_256x256@2x.png"
        COMMAND sips -z 512 512 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_512x512.png"
        COMMAND sips -z 1024 1024 "${_tgui_icon_png}" --out "${_tgui_iconset}/icon_512x512@2x.png"
        COMMAND iconutil -c icns "${_tgui_iconset}" -o "${_tgui_icns}"
        DEPENDS "${_tgui_icon_png}"
        COMMENT "Generating transmissiongui.icns"
        VERBATIM)
    add_custom_target(tgui_icns DEPENDS "${_tgui_icns}")
    set_source_files_properties("${_tgui_icns}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        GENERATED TRUE)
    target_sources(transmissiongui PRIVATE "${_tgui_icns}")
    add_dependencies(transmissiongui tgui_icns)
    set_target_properties(transmissiongui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_ICON_FILE "transmissiongui.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.transmissiongui.client"
        MACOSX_BUNDLE_BUNDLE_NAME "TransmissionGUI"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()
install(TARGETS transmissiongui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "transmissiongui.app/Contents/Resources")
    qt_generate_deploy_app_script(TARGET transmissiongui OUTPUT_SCRIPT deploy_script)
    install(SCRIPT "${deploy_script}")
    # Стандартные переводы Qt (I18N-02): macdeployqt .qm-файлы не копирует, а
    # main.cpp ищет qtbase_ru.qm в <bundle>/Contents/Translations — кладём его явно.
    get_target_property(_tgui_qt_tool Qt6::lrelease IMPORTED_LOCATION)
    if(_tgui_qt_tool)
        get_filename_component(_tgui_qt_bin "${_tgui_qt_tool}" DIRECTORY)
        find_program(_tgui_qtpaths NAMES qtpaths6 qtpaths HINTS "${_tgui_qt_bin}")
        if(_tgui_qtpaths)
            execute_process(COMMAND "${_tgui_qtpaths}" --query QT_INSTALL_TRANSLATIONS
                OUTPUT_VARIABLE _tgui_qt_translations
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
    endif()
    if(EXISTS "${_tgui_qt_translations}/qtbase_ru.qm")
        install(FILES "${_tgui_qt_translations}/qtbase_ru.qm"
            DESTINATION "transmissiongui.app/Contents/Translations")
    else()
        message(WARNING "qtbase_ru.qm не найден в «${_tgui_qt_translations}»: стандартные строки Qt останутся английскими в упакованном приложении")
    endif()
elseif(UNIX)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/transmissiongui.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    # Исходный PNG — 192x214; кладём в каталог ширины 192, чтобы тема не масштабировала иконку вверх.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/icons/transmission-upstream.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/192x192/apps"
        RENAME "transmissiongui.png")
endif()
