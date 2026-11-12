# Корневой CMake-файл проекта transmissionGUI — GUI-клиент демона Transmission (C++/Qt).
#
# Модули:
#   core — RPC-клиент и модели данных (чистый C++23, Qt Core/Network, без Widgets)
#   app  — графическое приложение на Qt 6 Widgets (русский UI)
#   tests— unit-тесты core (GoogleTest) [этап 1]

cmake_minimum_required(VERSION 3.28)

project(transmissiongui
    VERSION 0.1.0
    DESCRIPTION "Transmission daemon GUI client (C++/Qt)"
    LANGUAGES C CXX
)

# --- Стандарт языка ----------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json нужен clangd и IDE для подсказок.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- Опции -------------------------------------------------------------------
option(TGUI_WARNINGS_AS_ERRORS "Считать предупреждения ошибками" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# --- Зависимости --------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Svg LinguistTools)
find_package(ZLIB REQUIRED)
include(GNUInstallDirs)

# --- Опции сборки --------------------------------------------------------------
option(TGUI_BUILD_TESTS "Собирать unit-тесты (GoogleTest)" ON)
option(TGUI_BUILD_TOOLS "Собирать вспомогательные инструменты" ON)

# --- Модули ------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(app)

if(TGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(TGUI_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

set(CPACK_PACKAGE_NAME "transmissiongui")
set(CPACK_PACKAGE_VENDOR "transmissionGUI")
set(CPACK_PACKAGE_CONTACT "don100779@gmail.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/don10077/transmissionGUI")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt client for Transmission")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
elseif(UNIX)
    set(CPACK_GENERATOR DEB)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)
