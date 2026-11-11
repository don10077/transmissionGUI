# app: графическое приложение (Qt 6 Widgets).
add_executable(transmissiongui WIN32
    src/main.cpp
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
target_link_libraries(transmissiongui PRIVATE tgui_core Qt6::Widgets Qt6::Network z)
tgui_set_warnings(transmissiongui)

# i18n (W06): компилируем tgui_ru.ts -> tgui_ru.qm рядом с exe (<build>/app/translations/).
# Переводчик загружается в main.cpp; по умолчанию русский, fallback на английский, если .qm нет.
find_program(TGUI_LRELEASE lrelease REQUIRED)
set(TGUI_I18N_TS ${CMAKE_SOURCE_DIR}/translations/tgui_ru.ts)
set(TGUI_I18N_QM ${CMAKE_CURRENT_BINARY_DIR}/translations/tgui_ru.qm)
add_custom_command(
    OUTPUT ${TGUI_I18N_QM}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/translations
    COMMAND ${TGUI_LRELEASE} ${TGUI_I18N_TS} -qm ${TGUI_I18N_QM}
    DEPENDS ${TGUI_I18N_TS}
    COMMENT "Compiling translation tgui_ru.ts -> tgui_ru.qm"
    VERBATIM)
add_custom_target(tgui_translations DEPENDS ${TGUI_I18N_QM})
add_dependencies(transmissiongui tgui_translations)
