# Building on Unix-like systems

This fork supports Linux and OpenBSD. Windows and Android have their own
instructions. macOS, FreeBSD and Solaris are outside the supported platform
set; use upstream i2pd if you need them.

Make sure the dependencies from [requirements](requirements.md) are installed.

## Clone and build

```sh
git clone https://github.com/daviduhden/openi2pd.git
cd openi2pd
cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

The resulting binaries are `build-output/i2pd` and, when ncursesw was found,
`build-output/i2pd-tui`. Auxiliary tools are in `build-output/tools/`.

The POSIX make front-end performs the same steps:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=8 make
```

Do not pass `-j` to the front-end; it is forwarded by CMake, not by make.

## Tests

```sh
cmake -S build -B build-output -G Ninja -DBUILD_TESTING=ON
cmake --build build-output --parallel
ctest --test-dir build-output --output-on-failure
```

Fuzz targets are enabled separately with `-DWITH_FUZZERS=ON` and require
Clang.

## Install

```sh
sudo cmake --install build-output --prefix /usr
```

or, after configuring the prefix:

```sh
sudo make install
```

## CMake options

Pass options as `-D<key>=<value>`:

| Option | Default | Meaning |
|---|---|---|
| `CMAKE_BUILD_TYPE` | none | `Debug`, `Release`, `RelWithDebInfo` |
| `I2PD_BUILD_TOOLS` | ON | Build the auxiliary tools |
| `I2PD_BUILD_TUI` | ON | Build `i2pd-tui` if ncurses is found |
| `I2PD_BUILD_TESTS` | OFF | Alias for `BUILD_TESTING=ON` |
| `WITH_BINARY` | ON | Build the router |
| `WITH_LIBRARY` | ON | Install libraries and headers |
| `WITH_STATIC` | OFF | Static-link the router where supported |
| `WITH_UPNP` | ON | Require MiniUPnPc |
| `WITH_TORRENTS` | ON | Require Boost.JSON/URL (off on Android) |
| `WITH_HARDENING` | OFF | Stack protector, FORTIFY, RELRO/NOW |
| `WITH_ADDRSANITIZER` | OFF | AddressSanitizer |
| `WITH_THREADSANITIZER` | OFF | ThreadSanitizer |
| `WITH_UNDEFSANITIZER` | OFF | UndefinedBehaviorSanitizer |

List cached options with `cmake -L -S build -B build-output`.

## Debian/Ubuntu

```sh
sudo apt install build-essential cmake ninja-build \
  libboost-atomic-dev libboost-filesystem-dev \
  libboost-program-options-dev libboost-json-dev libboost-url-dev \
  libssl-dev zlib1g-dev
sudo apt install libminiupnpc-dev  # optional UPnP
```

Build a package with the maintained `debian/rules` (CMake + Ninja) or use the
manual build above. The old Makefile patches and EOL Ubuntu packaging were
removed.

## Arch/Manjaro

```sh
sudo pacman -S --needed base-devel cmake ninja boost openssl zlib
```

## Fedora/RHEL/CentOS Stream

```sh
sudo dnf install cmake ninja-build gcc gcc-c++ boost-devel \
  openssl-devel zlib-devel
```

MiniUPnPc is in `miniupnpc-devel`; static Boost is in `boost-static` if needed.

## OpenBSD

```sh
doas pkg_add cmake ninja boost
doas pkg_add miniupnpc   # optional
```

OpenSSL is provided as LibreSSL in base. Build with CMake/Ninja or with the
POSIX make front-end (`bmake`):

```sh
doas pkg_add ninja
cmake -S build -B build-output -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

The OpenBSD service layout and `pledge`/`unveil` notes are in
[contrib/openbsd](https://github.com/daviduhden/openi2pd/blob/master/contrib/openbsd/README.md). Do not use `gmake`: it is
not required by this tree.

## unsupported platforms

The CMake project fails early on macOS, FreeBSD and Solaris. Upstream i2pd may
still support them; this fork does not claim or test that support.
