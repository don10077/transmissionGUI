# transmissionGUI

C++23 / Qt 6 

Планирование и статус — в [WORKFLOW.md](WORKFLOW.md).

## Сборка (Windows, MSYS2 MinGW64)

Тулчейн: `D:\Install\MSYS2` (g++ 16.2, Qt 6.12, CMake ≥ 3.28).
Пресеты уже указаны в `CMakeUserPresets.json` (при отсутствии — скопируйте `.example`).

```powershell
cd D:\Work\transmissionGUI
& D:\Install\MSYS2\mingw64\bin\cmake.exe --preset win-debug
& D:\Install\MSYS2\mingw64\bin\cmake.exe --build --preset win-debug --parallel
& D:\Install\MSYS2\mingw64\bin\cmake.exe --preset win-debug # test (этап 1)
```

Итоговый бинарник: `build/win-debug/app/transmissiongui.exe`.

## Лицензия

GPL v3
