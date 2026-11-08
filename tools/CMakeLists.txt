# tools: вспомогательные инструменты (не часть GUI).
# daemon-check — dev-проверка подключения к реальному демону (этап 1, приёмка).
add_executable(tgui_daemon_check daemon-check.cpp)
target_link_libraries(tgui_daemon_check PRIVATE tgui_core)
tgui_set_warnings(tgui_daemon_check)