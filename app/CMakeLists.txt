# app: графическое приложение (Qt 6 Widgets).
add_executable(transmissiongui WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/torrentmodel.cpp
    src/connectdialog.cpp
    src/adddialog.cpp
    src/fileselectdialog.cpp
    src/groupedproxy.cpp
    # Явное перечисление Q_OBJECT-заголовков для AUTOMOC (как в core).
    src/mainwindow.h
    src/torrentmodel.h
    src/connectdialog.h
    src/adddialog.h
    src/fileselectdialog.h
    src/groupedproxy.h
)
target_include_directories(transmissiongui PRIVATE src)
target_link_libraries(transmissiongui PRIVATE tgui_core Qt6::Widgets Qt6::Network)
tgui_set_warnings(transmissiongui)
