# app: графическое приложение (Qt 6 Widgets).
add_executable(transmissiongui WIN32
    src/main.cpp
    src/mainwindow.cpp
)
target_include_directories(transmissiongui PRIVATE src)
target_link_libraries(transmissiongui PRIVATE tgui_core Qt6::Widgets)
tgui_set_warnings(transmissiongui)
