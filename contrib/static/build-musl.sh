#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: build musl.
#
# Build a static i2pd binary in Alpine with packaged OpenSSL, Boost,
# zlib and MiniUPnPc, using the main CMake graph. Install build
# dependencies with apk, build only i2pd, then strip the result.
#

# Run from the source root in Alpine with permission to install
# packages.
# JOBS sets CMake parallelism (default 4). Dependencies come from apk,
# not downloaded source trees. build-output/i2pd is replaced, then
# stripped.
#
# Requires root for apk and network access to Alpine repositories.
# The script installs build dependencies, runs no tests, and creates no
# package. It leaves build-output/ for the caller (contrib/static/
# package.sh and the manual packaging workflow consume it). Library,
# TUI and auxiliary tool builds are disabled so the result is one static
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
