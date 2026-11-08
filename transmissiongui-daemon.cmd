@echo off
rem Запуск transmissionGUI с автоподключением к домашнему демону.
set "PATH=D:\Install\MSYS2\mingw64\bin;%PATH%"
start "" "D:\Work\transmissionGUI\build\win-debug\app\transmissiongui.exe" --url "http://192.168.7.10:9091" --user admin --pass "A,hbdbfnehF"