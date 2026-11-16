@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Usage: build.cmd [debug^|release] [--no-pause] [--no-tests]
rem MSYS2_ROOT overrides the default installation locations.
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
set "BUILD_PROFILE="
if defined MSYS2_ROOT (
    call :findMsys2 "%MSYS2_ROOT%"
) else (
    call :findMsys2 "D:\Install\MSYS2"
    if not defined BUILD_PROFILE call :findMsys2 "%SystemDrive%\msys64"
)
if not defined BUILD_PROFILE (
    echo ERROR: No complete MSYS2 UCRT64 or MINGW64 toolchain found.
    echo Install cmake, ninja, gcc and g++ in one profile, or set MSYS2_ROOT to the installation directory.
    goto finish
)
rem Keep the existing MINGW64 output path used by local launchers.
if /I "%BUILD_PROFILE%"=="ucrt64" set "BUILD_DIR=ucrt64\%BUILD_DIR%"
set "PATH=%BUILD_BIN%;%PATH%"
pushd "%~dp0"
if errorlevel 1 goto finish
echo Configuring %BUILD_KIND% with MSYS2 %BUILD_PROFILE%...
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

:findMsys2
for %%P in (ucrt64 mingw64) do (
    call :findProfile "%~1" %%P
    if defined BUILD_PROFILE exit /b 0
)
exit /b 1

:findProfile
set "BUILD_BIN=%~1\%~2\bin"
for %%T in (cmake.exe ninja.exe gcc.exe g++.exe) do (
    if not exist "%BUILD_BIN%\%%T" exit /b 1
)
set "BUILD_PROFILE=%~2"
exit /b 0
