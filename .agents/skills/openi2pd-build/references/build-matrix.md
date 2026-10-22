<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd build matrix.
-->
# Build target and platform matrix

Use with `SKILL.md`. This is a static inventory; no configure/build is implied.

## Targets

| Target | Sources | Notes |
|---|---|---|
| `libi2pd` | `libi2pd/**` | core; static/PIC default |
| `libi2pdclient` | `libi2pd_client/**` | client/torrent protocols |
| `libi2pdlang` | `i18n/**` | language data |
| `i2pd_privsep` | `privsep/**` | IPC/supervisor/platform adapters |
| `i2pd` | `daemon/**` + libs | router daemon |
| `i2pd-tui` | `tui/**` | ncursesw (Unix) / Win32 (Windows); does not link router libs |
| tools | `tools/**` | 15 native tools + Perl client |
| tests | `tests/**` | CTest; platform-filtered |
| `i2pd_jni` | Android + shared daemon subset | Android only; no `i2pd_privsep` |

## Options and defaults

`WITH_LIBRARY=ON`, `WITH_BINARY=ON`, `I2PD_BUILD_TUI=ON`,
`I2PD_BUILD_TOOLS=ON`, `I2PD_BUILD_JNI=OFF`, `I2PD_INSTALL_DOCS=ON`,
`WITH_UPNP=ON`, `WITH_TORRENTS=ON`, `WITH_STATIC=OFF`, `WITH_HARDENING=OFF`,
`BUILD_TESTING=OFF`, `WITH_ADDRSANITIZER=OFF`, `WITH_THREADSANITIZER=OFF`,
`WITH_UNDEFSANITIZER=OFF`, `WITH_FUZZERS=OFF`, `I2PD_PERL_CRITIC=OFF`,
`I2PD_OPENSSL_EXPECT`, `I2PD_OPENSSL_EXPECT_VERSION`.

Disabling torrents must still configure a coherent core/client/TUI/tools graph
on Linux, OpenBSD and Windows. Do not make the Android graph depend on a
desktop supervisor.

## Platform lanes

| Lane | Configure/build consumer | Tests |
|---|---|---|
| Linux Clang/GCC | full graph, `BUILD_TESTING=ON`, TUI | full CTest + labels |
| Linux OpenSSL 3.x | distribution OpenSSL 3 | full CTest |
| Linux pinned OpenSSL | `I2PD_OPENSSL_EXPECT`/version pin | build + full CTest |
| Install components | Runtime/Tui/Libraries/Development/Tools/Documentation | staged tree + consumer package |
| Linux ASan+UBSan | full graph, sanitizer option | full CTest |
| Linux TSan | full graph, sanitizer option | full CTest |
| Fuzzing | `WITH_FUZZERS=ON`, Clang | eight bounded smoke targets |
| OpenBSD | CMake/Ninja, LibreSSL | full CTest, platform-only IPC/sandbox tests |
| Windows MSYS2/Clang64 | full graph | Windows CTest |
| Windows MSVC | TUI/API-focused | TUI/API/install |
| Android | Gradle + NDK r30 + vcpkg API 29 | unit/Lint/JNI/APK; instrumentation device-only |

## CMake/Make rules

- CMake source of truth; root `Makefile` and `tests/Makefile` are POSIX
  front-ends, not alternative rule sources.
- `cmake -S build -B <dir> -G Ninja`.
- No `file(GLOB ...)`, configure-time download, hidden external checkout,
  absolute install prefix or GNU-only make syntax.
- Explicit source lists and target-scoped dependencies.
- `I2PD_SERVICE_MANAGER` accepts `none`, `systemd`, `openrc`, `runit`, `s6`,
  `s6-rc`, `shepherd`, `sysvinit`; only one may be installed.

## Install components

`Runtime` (daemon/config/certificates), `Tui` (client/desktop/icons), `Libraries`,
`Development`, `Tools`, `Documentation` and `Service`. `Service` requires
`WITH_BINARY=ON`; exactly one manager is installed. Consumers require both
`Libraries` and `Development` for `find_package(i2pd CONFIG)`.

## Static checks

`build-system-static` asserts dependency floors, removed compatibility
branches, test/fuzzer inventory and options. Keep it coherent when changing the
graph. It is not a compile.
