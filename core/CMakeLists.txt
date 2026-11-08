# core: RPC-клиент Transmission и модели данных (чистый C++, без Widgets).
# Этап 0: утилиты форматирования; этап 1: jsonrpc, models, rpcclient, bencode.
add_library(tgui_core STATIC
    src/util.cpp
    src/bencode.cpp
    src/models.cpp
    src/jsonrpc.cpp
    src/rpcclient.cpp
)
target_include_directories(tgui_core PUBLIC include)
target_link_libraries(tgui_core PUBLIC Qt6::Core Qt6::Network)
tgui_set_warnings(tgui_core)
