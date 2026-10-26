#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: build musl.
#
# Builds a fully static i2pd binary inside Alpine using packaged OpenSSL,
# Boost, zlib and MiniUPnPc and the main CMake graph. It installs the build
# dependencies with apk, builds only the i2pd target and strips the result.
#

# Run from the source root inside Alpine with permission to install packages.
# JOBS sets CMake's parallelism (default 4). Dependencies come from apk rather
# than downloaded source trees; build-output/i2pd is replaced and stripped.
#
# Requires root for apk and network access for the Alpine repositories. The
# script only builds: it installs nothing, runs no tests and packages nothing,
# and leaves the build-output/ directory for the caller (contrib/static/
# package.sh and the manual packaging workflow consume it). The library and TUI
# builds plus the auxiliary tools are disabled so the result is one static
# router binary.
set -eu

JOBS=${JOBS:-4}
apk add --no-cache build-base cmake ninja perl linux-headers \
	boost-dev boost-static boost-container zlib-dev zlib-static openssl-dev openssl-libs-static \
	miniupnpc-dev

cmake -S build -B build-output -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DWITH_STATIC=ON \
	-DWITH_UPNP=ON \
	-DWITH_LIBRARY=OFF \
	-DI2PD_BUILD_TOOLS=OFF \
	-DI2PD_BUILD_TUI=OFF
cmake --build build-output --parallel "$JOBS" --target i2pd
strip build-output/i2pd
