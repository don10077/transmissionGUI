@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Usage: build.cmd [debug^|release] [--no-pause]
rem Override the installation path with the MSYS2_ROOT environment variable.
set "BUILD_KIND=Debug"
set "BUILD_DIR=win-debug"
set "BUILD_PAUSE=1"
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
) else (
    echo Usage: build.cmd [debug^|release] [--no-pause]
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
echo Building transmissiongui...
"%BUILD_BIN%\cmake.exe" --build "build/%BUILD_DIR%" --target transmissiongui --parallel
if errorlevel 1 goto failed
set "BUILD_RESULT=0"
echo.
echo Build succeeded: "%~dp0build\%BUILD_DIR%\app\transmissiongui.exe"
goto leave
:failed
echo.
echo ERROR: Build failed. See the messages above.
echo If transmissiongui.exe is running, close it before rebuilding.
:leave
popd
:finish
if "%BUILD_PAUSE%"=="1" pause
exit /b %BUILD_RESULT%
