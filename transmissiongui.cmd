@echo off
rem Запуск transmissionGUI (нужны Qt6-библиотеки из MSYS2 MinGW64).
set "PATH=D:\Install\MSYS2\mingw64\bin;%PATH%"
start "" "D:\Work\transmissionGUI\build\win-debug\app\transmissiongui.exe"