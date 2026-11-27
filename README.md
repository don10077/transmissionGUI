# transmissionGUI

**English** | [Русский](README.ru.md)

**Download** · [Release v1.1.4](https://github.com/don10077/transmissionGUI/releases/tag/v1.1.4) · [All releases](https://github.com/don10077/transmissionGUI/releases)

- Windows x64 — [Installer (.exe)](https://github.com/don10077/transmissionGUI/releases/download/v1.1.4/transmissiongui-1.1.4-windows-x64-setup.exe) · [Portable (.zip)](https://github.com/don10077/transmissionGUI/releases/download/v1.1.4/transmissiongui-windows-x64.zip)
- macOS — [Disk image (.dmg)](https://github.com/don10077/transmissionGUI/releases/download/v1.1.4/transmissiongui-1.1.4-Darwin.dmg)
- Debian / Ubuntu (amd64) — [Package (.deb)](https://github.com/don10077/transmissionGUI/releases/download/v1.1.4/transmissiongui_1.1.4_amd64.deb)

A native desktop client and **Transmission GUI** for the **Transmission** BitTorrent daemon,
written in **C++23 / Qt 6**. It talks to the daemon over its JSON-RPC API and gives you a
single window for managing torrents on a local or remote machine — no browser and no
`transmission-remote` required. Sequential download, labels, speed schedules, detailed
statistics and connection profiles are built in.

Project website: <https://don10077.github.io/transmissionGUI/>

![Build and test](https://github.com/don10077/transmissionGUI/actions/workflows/build.yml/badge.svg)

## Features

**Connections**

- HTTP and HTTPS daemons, basic authentication, custom RPC path, TLS client certificates
  and HTTP/SOCKS5 proxies.
- Automatic `X-Transmission-Session-Id` handling (HTTP 409), RPC path redirects (HTTP 301),
  configurable reconnect after connection loss.
- Multiple connection profiles with per-profile path mapping, folder history and speed-limit
  presets; quick switching from the toolbar or the connection menu.
- Command-line auto-connect: `--url`, `--user`, `--pass`, `--path`.

**Torrent list**

- Status icons, progress, size, download/upload rates, ETA, peers, ratio and labels.
- Sortable columns, configurable column set and order, search by name.
- Filter sidebar by status (All, Downloading, Completed, Active, Inactive, Stopped, Error,
  Waiting) plus grouping by folder, label or tracker with counters.
- Configurable refresh interval for the active and minimized window, optional rate smoothing,
  status-bar totals and daemon version.

**Torrent management**

- Start, stop, force-start, remove (with or without downloaded data), queue reordering,
  bandwidth priority, labels, rename, verify and reannounce.
- Properties dialog with batch editing of explicitly selected fields; moving one or several
  torrents with a single request.
- Details panel for the selected torrent: **General**, **Trackers**, **Peers**, **Files**
  (want/skip and priorities) and **Statistics**.

**Adding torrents**

- `.torrent` files, magnet links and tracker URLs; drag and drop; opening from the OS;
  optional clipboard monitoring and a watch folder.
- Destination folder with free-space display, file selection, peer limit, "save as",
  start-immediately and sequential download (Transmission 4.1+).
- Corrupted `.torrent` files: recognizable length and tracker-scheme errors are repaired in
  memory before sending; the original file and its `info` hashes stay untouched.

**Interface and integration**

- Custom light theme shared by all dialogs; toolbar styles (icons only, text beside/under
  icons), adjustable font scale.
- Tray icon with quick actions, minimize/close to tray and notifications.
- Optional `.torrent` and magnet file associations; a second instance forwards opened
  torrents to the running window.
- Application and daemon settings, speed schedules, label catalog, GeoIP database download,
  peer reverse DNS, blocklist update, settings backup/import and update check.
- English UI with a bundled Russian translation; additional languages can be shipped as
  `tgui_<lang>.qm`.

## Sequential download

Transmission 4.1+ (RPC 18) can download a torrent in order instead of randomly:

- **For matching files automatically:** *Application options → Sequential download*, enable
  “Enable automatically for files with these extensions” and list the extensions.
- **For a single torrent:** tick **Sequential download** in the *Add torrent* dialog, or open
  *Properties → General* and toggle it there.

Detailed guide: [How to enable sequential download in Transmission](https://don10077.github.io/transmissionGUI/sequential-download/) ·
[на русском](https://don10077.github.io/transmissionGUI/ru/sequential-download/).

## Transmission compatibility

| Daemon | RPC mode |
|---|---|
| Transmission 2.x / 3.x / 4.0.x | Legacy JSON-RPC (`session-get`, `torrent-get`, …) |
| Transmission 4.1+ | JSON-RPC 2.0, selected automatically from `X-Transmission-Rpc-Version` |

Some features depend on the daemon version: sequential download requires Transmission 4.1
(RPC 18), label renaming requires 4.0 (RPC 16). See the
[Transmission RPC specification](https://github.com/transmission/transmission/blob/4.1.0/docs/rpc-spec.md).

## Requirements

- **CMake 3.28+** and a C++23 compiler
- **Qt 6**: Core, Network, Widgets, Svg and LinguistTools
- **zlib** and **libarchive**
- **GoogleTest** (tests only)

Windows builds use **MSYS2 / UCRT64**. Linux and macOS are supported as well; all three
platforms are built and tested in CI.

## Building

### Windows (MSYS2 UCRT64)

The quickest way is the helper script. It configures CMake, builds the app and tests, and runs
the test suite:

```bat
build.cmd
build.cmd release
build.cmd ci --no-pause
build.cmd debug --no-pause --no-tests
```

Before compilation, `build.cmd` runs the same theme token and translation freshness checks
as Windows CI, including with `--no-tests`. If `lupdate` changes a `.ts` file, the build
stops: review the changes, complete new translations, rerun the build and include the `.ts`
file in your commit. The `ci` mode builds Release with warnings as errors and runs tests;
packaging and installation checks remain separate steps.

By default the script uses MSYS2 from `D:\Install\MSYS2`; set `MSYS2_ROOT` to use another
installation. Install the packages from an **MSYS2 UCRT64** shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-svg,qt6-tools,qt6-declarative,qt6-translations,openssl,zlib,libarchive,gtest}
```

Alternatively, use the CMake presets. Copy `CMakeUserPresets.json.example` to
`CMakeUserPresets.json` (this file is git-ignored), adjust the MSYS2 path if needed, and run:

```powershell
cmake --preset win-debug
cmake --build --preset win-debug --parallel
ctest --preset win-debug
```

Make sure `ucrt64\bin` is on `PATH` when invoking CMake directly. Both Windows build
methods use `build/ucrt64/`.

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

### Build outputs

| Path | Contents |
|---|---|
| `build/<preset>/app/transmissiongui` (`TransmissionGUI.exe`) | application |
| `build/<preset>/tests/tgui_tests` (`tgui_tests.exe`) | unit and GUI tests |
| `build/<preset>/tools/tgui_daemon_check` (`tgui_daemon_check.exe`) | daemon connectivity check |

## Running

```sh
transmissiongui --url http://192.168.1.10:9091 --user admin --pass secret
```

Without arguments the application opens the connection dialog and remembers the last used
profiles. The port is mandatory: Transmission listens on `9091` by default, and omitting it
makes the client try port 80.

Useful options:

| Option | Description |
|---|---|
| `--url <url>` | daemon URL, e.g. `http://host:9091` |
| `--user <name>` | RPC username |
| `--pass <password>` | RPC password |
| `--path <path>` | RPC path (default `/transmission/rpc`) |
| `--smoke-test` | start with isolated settings and exit automatically |

Positional arguments may be local `.torrent` files or magnet links; they are added after
startup. If another instance is already running, the torrents are forwarded to it.

Settings are stored per user with `QSettings`. The application can export them to an INI file
and import them back from the **Torrent** menu.

## Tests

The test suite uses **GoogleTest** and a local mock HTTP server, so it runs without a daemon:

```sh
QT_QPA_PLATFORM=offscreen ctest --preset release
```

It covers bencode, RPC utilities, the legacy/JSON-RPC transport, session-id
handshake, redirects, timeouts, GUI models, dialogs, the theme and window layout. A separate
smoke test starts the application with `--smoke-test`. Tests that need a live daemon are
skipped unless one is configured.

For manual checks against a real daemon:

```sh
tgui_daemon_check http://127.0.0.1:9091 --user admin --pass secret
```

`--test-add <file.torrent>` additionally runs an add → file selection → remove round trip.

## Packaging

**Windows portable ZIP** (after a Release build):

```powershell
build.cmd release --no-tests
powershell -File tools/package-windows.ps1
```

The archive is written to `build/packages/transmissiongui-windows-x64.zip`. The script runs
`windeployqt` and collects the required MinGW/OpenSSL libraries. Pass `-QtBin` and
`-BuildDir` if your paths differ from the defaults.

**Windows installer** — run `powershell -File tools/package-installer.ps1` after
building the portable ZIP. Install NSIS 3 and put `makensis.exe` on `PATH`, or pass
its path with `-MakeNsis`. The installer adds a Start menu shortcut and an uninstaller
for the current user.

**Linux DEB / macOS DMG** (CPack):

```sh
cpack --config build/release/CPackConfig.cmake -B build/packages
```

## CI and releases

GitHub Actions runs four groups in parallel: Windows (installer and portable), Linux (DEB),
ASan/UBSan tests, and macOS (DMG). Inside the Windows workflow, a separate clean runner
checks portable startup, installation, upgrade, and uninstallation after packaging.

The `Release` job waits for every group to succeed. It publishes the packages,
`SHA256SUMS.txt`, and release notes only when pushing a `v<version>` tag
matching `project(VERSION ...)` in `CMakeLists.txt`. Publication is skipped for ordinary
pushes and pull requests; packages remain available as workflow artifacts. Release notes are read
from `docs/releases/<tag>.md` when present, or generated automatically otherwise.

## Project layout

```
core/          RPC client, models, bencode and utilities (no GUI)
app/           Qt Widgets application, dialogs and theme
tests/         GoogleTest suite and mock HTTP server
tools/         tgui_daemon_check helper
translations/  Russian translation (tgui_ru.ts)
cmake/         Compiler warning helpers
```

The UI source strings are English. `translations/tgui_ru.ts` is compiled to
`tgui_ru.qm` during the build and embedded into the executable. To add a language, create
`translations/tgui_<lang>.ts`, translate it, and place the compiled `.qm` next to the
executable or into the resource bundle.

## FAQ

**What is transmissionGUI?**
A free, open-source Transmission GUI for Windows, Linux and macOS. It connects to the
Transmission daemon over RPC and manages its torrents in a native desktop window.

**How do I enable sequential download in Transmission?**
Use *Application options → Sequential download* for automatic rules, or tick
**Sequential download** in the *Add torrent* dialog / *Properties → General* for one torrent.
The daemon must be Transmission 4.1 or newer (RPC 18).

**Which Transmission versions are supported?**
Transmission 2.x, 3.x and 4.0.x use the legacy JSON-RPC API; 4.1+ is detected automatically
and uses JSON-RPC 2.0.

**Does it need a web browser?**
No. transmissionGUI is a standalone Qt application and does not embed the Transmission web UI.

**Is it free?**
Yes. transmissionGUI is distributed under GPL-3.0. See the
[installation page](https://don10077.github.io/transmissionGUI/#install) for packaged builds
and source build instructions.

## License

[GNU General Public License v3.0](LICENSE).

## Links

- Website and user guide: <https://don10077.github.io/transmissionGUI/>
- Repository: <https://github.com/don10077/transmissionGUI>
- Issues: <https://github.com/don10077/transmissionGUI/issues>
- Releases: <https://github.com/don10077/transmissionGUI/releases>
- Contact: <don100779@gmail.com>
