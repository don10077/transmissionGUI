@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Usage: build.cmd [debug^|release^|ci] [--no-pause] [--no-tests]
rem MSYS2_ROOT overrides the default installation locations.
set "BUILD_KIND=Debug"
set "BUILD_DIR=win-debug"
set "BUILD_PAUSE=1"
set "BUILD_TESTS=1"
set "BUILD_RESULT=1"
set "BUILD_WARNINGS_AS_ERRORS=OFF"
:args
if "%~1"=="" goto setup
if /I "%~1"=="debug" (
    set "BUILD_KIND=Debug"
    set "BUILD_DIR=win-debug"
    set "BUILD_WARNINGS_AS_ERRORS=OFF"
) else if /I "%~1"=="release" (
    set "BUILD_KIND=Release"
    set "BUILD_DIR=win-release"
    set "BUILD_WARNINGS_AS_ERRORS=OFF"
) else if /I "%~1"=="ci" (
    set "BUILD_KIND=Release"
    set "BUILD_DIR=win-ci"
    set "BUILD_WARNINGS_AS_ERRORS=ON"
) else if /I "%~1"=="--no-pause" (
    set "BUILD_PAUSE=0"
) else if /I "%~1"=="--no-tests" (
    set "BUILD_TESTS=0"
) else (
    echo Usage: build.cmd [debug^|release^|ci] [--no-pause] [--no-tests]
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
    echo ERROR: No complete MSYS2 UCRT64 toolchain found.
    echo Install UCRT64 cmake, ninja, gcc and g++, or set MSYS2_ROOT to the installation directory.
    goto finish
)
set "BUILD_DIR=ucrt64\%BUILD_DIR%"
set "PATH=%BUILD_BIN%;%PATH%"
pushd "%~dp0"
if errorlevel 1 goto finish
echo Configuring %BUILD_KIND% with MSYS2 %BUILD_PROFILE%...
"%BUILD_BIN%\cmake.exe" -S . -B "build/%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_KIND% -DTGUI_WARNINGS_AS_ERRORS=%BUILD_WARNINGS_AS_ERRORS% -DCMAKE_C_COMPILER="%BUILD_BIN%/gcc.exe" -DCMAKE_CXX_COMPILER="%BUILD_BIN%/g++.exe" -DCMAKE_MAKE_PROGRAM="%BUILD_BIN%/ninja.exe"
if errorlevel 1 goto failed
echo Checking source consistency...
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-source.ps1 -BuildDir "build/%BUILD_DIR%" -CMake "%BUILD_BIN%/cmake.exe"
if errorlevel 1 goto failed
rem Close only our own executables from this build directory: a live exe locks the
rem output file and breaks the link. Other copies (installed/portable) stay untouched.
powershell -NoProfile -Command "Get-Process transmissiongui,tgui_tests -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith('%~dp0build\%BUILD_DIR%', [StringComparison]::OrdinalIgnoreCase) } | Stop-Process -Force" >nul 2>&1
echo Building transmissiongui...
"%BUILD_BIN%\cmake.exe" --build "build/%BUILD_DIR%" --target transmissiongui --parallel
if errorlevel 1 goto failed
if "%BUILD_TESTS%"=="1" (
    echo Building tests...
    "%BUILD_BIN%\cmake.exe" --build "build/%BUILD_DIR%" --target tgui_tests --parallel
    if errorlevel 1 goto failed
    echo Running tests...
    set "QT_QPA_PLATFORM=offscreen"
    "%BUILD_BIN%\ctest.exe" --test-dir "build/%BUILD_DIR%" --output-on-failure
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
echo Build succeeded: "%~dp0build\%BUILD_DIR%\app\TransmissionGUI.exe"
goto leave
:failed
echo.
echo ERROR: Build (or tests) failed. See the messages above.
echo If TransmissionGUI.exe is running, close it before rebuilding.
:leave
popd
:finish
if "%BUILD_PAUSE%"=="1" pause
exit /b %BUILD_RESULT%

:findMsys2
call :findProfile "%~1" ucrt64
exit /b %errorlevel%

:findProfile
set "BUILD_BIN=%~1\%~2\bin"
for %%T in (cmake.exe ninja.exe gcc.exe g++.exe) do (
    if not exist "%BUILD_BIN%\%%T" exit /b 1
)
set "BUILD_PROFILE=%~2"
exit /b 0
