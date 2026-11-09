@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Usage: build.cmd [debug^|release] [--no-pause]
rem Override the installation path with the MSYS2_ROOT environment variable.
set "BUILD_KIND=Debug"
set "BUILD_DIR=win-debug"
set "BUILD_PAUSE=1"
set "BUILD_TESTS=1"
set "BUILD_RESULT=1"
:args
if "%~1"=="" goto setup
if /I "%~1"=="debug" (
    set "BUILD_KIND=Debug"
    set "BUILD_DIR=win-debug"
) else if /I "%~1"=="release" (
    set "BUILD_KIND=Release"
    set "BUILD_DIR=win-release"
) else if /I "%~1"=="--no-pause" (
    set "BUILD_PAUSE=0"
) else if /I "%~1"=="--no-tests" (
    set "BUILD_TESTS=0"
) else (
    echo Usage: build.cmd [debug^|release] [--no-pause] [--no-tests]
    exit /b 2
)
shift
goto args
:setup
if not defined MSYS2_ROOT set "MSYS2_ROOT=D:\Install\MSYS2"
set "BUILD_BIN=%MSYS2_ROOT%\mingw64\bin"
for %%T in (cmake.exe ninja.exe gcc.exe g++.exe) do (
    if not exist "%BUILD_BIN%\%%T" (
        echo ERROR: Missing "%BUILD_BIN%\%%T".
        echo Install the MSYS2 MinGW64 tools or set MSYS2_ROOT.
        goto finish
    )
)
set "PATH=%BUILD_BIN%;%PATH%"
pushd "%~dp0"
if errorlevel 1 goto finish
echo Configuring %BUILD_KIND%...
"%BUILD_BIN%\cmake.exe" -S . -B "build/%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_KIND% -DCMAKE_C_COMPILER="%BUILD_BIN%/gcc.exe" -DCMAKE_CXX_COMPILER="%BUILD_BIN%/g++.exe" -DCMAKE_MAKE_PROGRAM="%BUILD_BIN%/ninja.exe" -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
if errorlevel 1 goto failed
rem Close any running instance: a live exe locks the output file and breaks the link.
taskkill /IM transmissiongui.exe /F >nul 2>&1
taskkill /IM tgui_tests.exe /F >nul 2>&1
echo Building transmissiongui...
"%BUILD_BIN%\cmake.exe" --build "build/%BUILD_DIR%" --target transmissiongui --parallel
if errorlevel 1 goto failed
echo Building tests...
"%BUILD_BIN%\cmake.exe" --build "build/%BUILD_DIR%" --target tgui_tests --parallel
if errorlevel 1 goto failed
if "%BUILD_TESTS%"=="1" (
    echo Running tests...
    set "QT_QPA_PLATFORM=offscreen"
    "build\%BUILD_DIR%\tests\tgui_tests.exe"
    if errorlevel 1 (
        echo.
        echo ERROR: Tests failed. See output above.
        goto failed
    )
) else (
    echo Skipping tests - no-tests option.
)
set "BUILD_RESULT=0"
echo.
echo Build succeeded: "%~dp0build\%BUILD_DIR%\app\transmissiongui.exe"
goto leave
:failed
echo.
echo ERROR: Build (or tests) failed. See the messages above.
echo If transmissiongui.exe is running, close it before rebuilding.
:leave
popd
:finish
if "%BUILD_PAUSE%"=="1" pause
exit /b %BUILD_RESULT%
