# transmissionGUI

[English](README.md) | **Русский**

**Скачать** · [Релиз v1.0.0](https://github.com/don10077/transmissionGUI/releases/tag/v1.0.0) · [Все релизы](https://github.com/don10077/transmissionGUI/releases)

- Windows x64 — [Установщик (.exe)](https://github.com/don10077/transmissionGUI/releases/download/v1.0.0/transmissiongui-1.0.0-windows-x64-setup.exe) · [Портативная версия (.zip)](https://github.com/don10077/transmissionGUI/releases/download/v1.0.0/transmissiongui-windows-x64.zip)
- macOS — [Образ диска (.dmg)](https://github.com/don10077/transmissionGUI/releases/download/v1.0.0/transmissiongui-1.0.0-Darwin.dmg)
- Debian / Ubuntu (amd64) — [Пакет (.deb)](https://github.com/don10077/transmissionGUI/releases/download/v1.0.0/transmissiongui_1.0.0_amd64.deb)

Нативный клиент и **GUI для Transmission** — битторент-демона **Transmission**, написанный
на **C++23 / Qt 6**. Работает через штатный JSON-RPC API и даёт одно окно для управления
торрентами на локальной или удалённой машине — без браузера и `transmission-remote`.
Последовательная загрузка, метки, расписание скоростей, подробная статистика и профили
подключений уже встроены.

Сайт проекта: <https://don10077.github.io/transmissionGUI/ru/>

![Build and test](https://github.com/don10077/transmissionGUI/actions/workflows/build.yml/badge.svg)

## Возможности

**Подключения**

- Демоны по HTTP и HTTPS, базовая аутентификация, свой RPC-путь, клиентские
  TLS-сертификаты и прокси HTTP/SOCKS5.
- Автоматическая обработка `X-Transmission-Session-Id` (HTTP 409), редиректы RPC-пути
  (HTTP 301), настраиваемое переподключение после обрыва связи.
- Несколько профилей подключений: сопоставление путей, история папок и быстрые лимиты
  скоростей; переключение с панели инструментов или из меню подключения.
- Автоподключение из командной строки: `--url`, `--user`, `--pass`, `--path`.

**Список торрентов**

- Иконки статуса, прогресс, размер, скорости загрузки и отдачи, ETA, пиры, рейтинг и метки.
- Сортировка по столбцам, настройка набора и порядка столбцов, поиск по имени.
- Боковая панель фильтров по статусу (Все, Загружаются, Завершённые, Активные, Неактивные,
  Остановленные, Ошибка, Ожидание) и группировка по папке, метке или трекеру со счётчиками.
- Настраиваемый интервал обновления для активного и свёрнутого окна, сглаживание скоростей,
  итоги в строке состояния и версия демона.

**Управление торрентами**

- Запуск, остановка, принудительный запуск, удаление (с данными и без), изменение очереди,
  приоритет полосы, метки, переименование, проверка и повторный announce.
- Диалог свойств с пакетным изменением явно выбранных полей; перенос одного или нескольких
  торрентов одним запросом.
- Панель деталей выбранного торрента: **General**, **Trackers**, **Peers**, **Files**
  (want/skip и приоритеты) и **Statistics**.

**Добавление торрентов**

- Файлы `.torrent`, magnet-ссылки и URL трекеров; перетаскивание; открытие из ОС;
  опциональное добавление из буфера обмена и отслеживаемая папка.
- Папка загрузки со свободным местом, выбор файлов, лимит пиров, «сохранить как»,
  запуск сразу и последовательная загрузка (Transmission 4.1+).
- Повреждённые `.torrent`: распознаваемые ошибки длины и схемы URL трекеров исправляются
  в памяти перед отправкой; исходный файл и хеши `info` не меняются.

**Интерфейс и интеграция**

- Единая светлая тема для всех диалогов; стили панели инструментов (только иконки,
  текст рядом с иконками или под ними), масштаб шрифта.
- Значок в трее с быстрыми действиями, сворачивание/закрытие в трей и уведомления.
- Опциональные ассоциации `.torrent` и magnet-ссылок; второй экземпляр передаёт открытые
  торренты уже запущенному окну.
- Настройки приложения и демона, расписание скоростей, каталог меток, загрузка базы GeoIP,
  обратный DNS для пиров, обновление blocklist, резервное копирование/импорт настроек
  и проверка обновлений.
- Английский интерфейс со встроенным русским переводом; другие языки можно добавить
  файлами `tgui_<lang>.qm`.

## Последовательная загрузка

Transmission 4.1+ (RPC 18) умеет скачивать торрент по порядку, а не случайными кусками:

- **Автоматически для подходящих файлов:** *Параметры приложения → Последовательное
  скачивание*, включите «Включать автоматически для файлов с расширениями» и укажите список
  расширений.
- **Для одного торрента:** поставьте галочку **Последовательная загрузка** в диалоге
  добавления торрента или в *Свойства → Общие*.

Подробная инструкция: [Последовательная загрузка в Transmission](https://don10077.github.io/transmissionGUI/ru/sequential-download/) ·
[in English](https://don10077.github.io/transmissionGUI/sequential-download/).

## Совместимость с Transmission

| Демон | Режим RPC |
|---|---|
| Transmission 2.x / 3.x / 4.0.x | Legacy JSON-RPC (`session-get`, `torrent-get`, …) |
| Transmission 4.1+ | JSON-RPC 2.0, выбирается автоматически по `X-Transmission-Rpc-Version` |

Часть возможностей зависит от версии демона: последовательная загрузка требует
Transmission 4.1 (RPC 18), переименование меток — 4.0 (RPC 16). См.
[спецификацию RPC Transmission](https://github.com/transmission/transmission/blob/4.1.0/docs/rpc-spec.md).

## Требования

- **CMake 3.28+** и компилятор с C++23
- **Qt 6**: Core, Network, Widgets, Svg и LinguistTools
- **zlib** и **libarchive**
- **GoogleTest** (только для тестов)

Сборка под Windows использует **MSYS2 / UCRT64**. Linux и macOS также поддерживаются;
все три платформы собираются и тестируются в CI.

## Сборка

### Windows (MSYS2 UCRT64)

Самый простой способ — скрипт-помощник. Он настраивает CMake, собирает приложение и тесты,
а затем запускает тестовый набор:

```bat
build.cmd
build.cmd release
build.cmd debug --no-pause --no-tests
```

По умолчанию скрипт использует MSYS2 из `D:\Install\MSYS2`; задайте `MSYS2_ROOT`, чтобы
указать другой путь. Установите пакеты из оболочки **MSYS2 UCRT64**:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-svg,qt6-tools,qt6-declarative,qt6-translations,openssl,zlib,libarchive,gtest}
```

Можно использовать CMake-пресеты. Скопируйте `CMakeUserPresets.json.example` в
`CMakeUserPresets.json` (файл исключён из git), при необходимости поправьте путь к MSYS2
и выполните:

```powershell
cmake --preset win-debug
cmake --build --preset win-debug --parallel
ctest --preset win-debug
```

При запуске CMake напрямую добавьте `ucrt64\bin` в `PATH`. Оба способа сборки под Windows
используют каталог `build/ucrt64/`.

### Linux

```sh
sudo apt install cmake ninja-build qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-l10n-tools \
    libgtest-dev zlib1g-dev libarchive-dev libxkbcommon-dev dpkg-dev file
cmake --preset release
cmake --build --preset release --parallel
QT_QPA_PLATFORM=offscreen ctest --preset release
```

### macOS

```sh
brew install qt ninja googletest libarchive
cmake --preset release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" -DLibArchive_ROOT="$(brew --prefix libarchive)"
cmake --build --preset release --parallel
QT_QPA_PLATFORM=offscreen ctest --preset release
```

### Результаты сборки

| Путь | Содержимое |
|---|---|
| `build/<preset>/app/transmissiongui` (`TransmissionGUI.exe`) | приложение |
| `build/<preset>/tests/tgui_tests` (`tgui_tests.exe`) | unit- и GUI-тесты |
| `build/<preset>/tools/tgui_daemon_check` (`tgui_daemon_check.exe`) | проверка подключения к демону |

## Запуск

```sh
transmissiongui --url http://192.168.1.10:9091 --user admin --pass secret
```

Без аргументов приложение открывает диалог подключения и помнит последние использованные
профили. Порт указывать обязательно: Transmission по умолчанию слушает `9091`, а без порта
клиент попытается подключиться к порту 80.

Полезные параметры:

| Параметр | Описание |
|---|---|
| `--url <url>` | адрес демона, например `http://host:9091` |
| `--user <name>` | имя пользователя RPC |
| `--pass <password>` | пароль RPC |
| `--path <path>` | RPC-путь (по умолчанию `/transmission/rpc`) |
| `--smoke-test` | запуск с изолированными настройками и автоматическим выходом |

Позиционные аргументы могут быть локальными `.torrent` файлами или magnet-ссылками: они
добавляются после запуска. Если приложение уже запущено, торренты передаются ему.

Настройки хранятся для каждого пользователя через `QSettings`. Приложение умеет
экспортировать их в INI-файл и импортировать обратно из меню **Torrent**.

## Тесты

Тестовый набор использует **GoogleTest** и локальный мок HTTP-сервера, поэтому работает
без демона:

```sh
QT_QPA_PLATFORM=offscreen ctest --preset release
```

Проверяются bencode, утилиты RPC, legacy/JSON-RPC транспорт, handshake session-id,
редиректы, таймауты, GUI-модели, диалоги, тема и раскладка окна. Отдельный smoke-тест
запускает приложение с `--smoke-test`. Тесты, которым нужен живой демон, пропускаются,
если он не настроен.

Для ручной проверки с реальным демоном:

```sh
tgui_daemon_check http://127.0.0.1:9091 --user admin --pass secret
```

`--test-add <file.torrent>` дополнительно выполняет цикл добавление → выбор файлов → удаление.

## Пакеты

**Portable ZIP для Windows** (после Release-сборки):

```powershell
build.cmd release --no-tests
powershell -File tools/package-windows.ps1
```

Архив создаётся в `build/packages/transmissiongui-windows-x64.zip`. Скрипт запускает
`windeployqt` и собирает нужные библиотеки MinGW/OpenSSL. Если пути отличаются от
стандартных, передайте `-QtBin` и `-BuildDir`.

**Установщик Windows** — после сборки portable ZIP запустите
`powershell -File tools/package-installer.ps1`. Установите NSIS 3 и добавьте
`makensis.exe` в `PATH` или передайте путь через `-MakeNsis`. Установщик добавляет
ярлык в меню «Пуск» и программу удаления для текущего пользователя.

**Linux DEB / macOS DMG** (CPack):

```sh
cpack --config build/release/CPackConfig.cmake -B build/packages
```

## Структура проекта

```
core/          RPC-клиент, модели, bencode и утилиты (без GUI)
app/           приложение Qt Widgets, диалоги и тема
tests/         тесты GoogleTest и мок HTTP-сервер
tools/         вспомогательная утилита tgui_daemon_check
translations/  русский перевод (tgui_ru.ts)
cmake/         помощники для предупреждений компилятора
```

Исходные строки интерфейса — на английском. `translations/tgui_ru.ts` компилируется в
`tgui_ru.qm` во время сборки и встраивается в исполняемый файл. Чтобы добавить язык,
создайте `translations/tgui_<lang>.ts`, переведите его и положите скомпилированный `.qm`
рядом с исполняемым файлом или в ресурсы.

## Вопросы и ответы

**Что такое transmissionGUI?**
Бесплатный GUI для Transmission с открытым исходным кодом для Windows, Linux и macOS.
Подключается к демону Transmission по RPC и управляет торрентами в нативном окне.

**Как включить последовательную загрузку в Transmission?**
Через *Параметры приложения → Последовательная загрузка* для автоматических правил или
галочкой **Последовательная загрузка** в диалоге добавления / *Свойства → General* для
одного торрента. Нужен демон Transmission 4.1 или новее (RPC 18).

**Какие версии Transmission поддерживаются?**
Transmission 2.x, 3.x и 4.0.x используют legacy JSON-RPC API; для 4.1+ режим JSON-RPC 2.0
определяется автоматически.

**Нужен ли браузер?**
Нет. transmissionGUI — самостоятельное приложение на Qt, веб-интерфейс Transmission
не используется.

**Программа бесплатная?**
Да. transmissionGUI распространяется под GPL-3.0. Ссылки на готовые сборки и инструкции
по сборке из исходников есть на [странице установки](https://don10077.github.io/transmissionGUI/ru/#install).

## Лицензия

[GNU General Public License v3.0](LICENSE).

## Ссылки

- Сайт и руководство: <https://don10077.github.io/transmissionGUI/ru/>
- Репозиторий: <https://github.com/don10077/transmissionGUI>
- Ошибки и предложения: <https://github.com/don10077/transmissionGUI/issues>
- Релизы: <https://github.com/don10077/transmissionGUI/releases>
- Контакт: <don100779@gmail.com>
