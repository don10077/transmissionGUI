# app: графическое приложение (Qt 6 Widgets).
add_executable(transmissiongui WIN32
    src/main.cpp
    src/theme.cpp
    src/mainwindow.cpp
    src/torrentmodel.cpp
    src/addlinkdialog.cpp
    src/connectdialog.cpp
    src/adddialog.cpp
    src/fileselectdialog.cpp
    src/groupedproxy.cpp
    src/delegates.cpp
    src/filterpanel.cpp
    src/detailmodels.cpp
    src/propsform.cpp
    src/statsform.cpp
    src/propsdialog.cpp
    src/movedialog.cpp
    # Явное перечисление Q_OBJECT-заголовков для AUTOMOC (как в core).
    src/mainwindow.h
    src/theme.h
    src/torrentmodel.h
    src/addlinkdialog.h
    src/connectdialog.h
    src/adddialog.h
    src/fileselectdialog.h
    src/groupedproxy.h
    src/delegates.h
    src/filterpanel.h
    src/detailmodels.h
    src/propsform.h
    src/statsform.h
    src/propsdialog.h
    src/movedialog.h
)
target_include_directories(transmissiongui PRIVATE src)
target_link_libraries(transmissiongui PRIVATE tgui_core Qt6::Widgets Qt6::Network)
tgui_set_warnings(transmissiongui)
