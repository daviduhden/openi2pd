#!/bin/sh
# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# Fully static openi2pd build, run from the source tree inside Alpine.
# No source code is downloaded: OpenSSL, Boost, zlib and MiniUPnPc come from
# Alpine packages, and the project is built by the main CMake graph.
set -eu

JOBS=${JOBS:-4}
apk add --no-cache build-base cmake ninja perl linux-headers \
  boost-dev boost-static zlib-dev zlib-static openssl-dev openssl-libs-static \
  miniupnpc-dev miniupnpc-static

cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_STATIC=ON \
  -DWITH_UPNP=ON \
  -DWITH_LIBRARY=OFF \
  -DI2PD_BUILD_TOOLS=OFF \
  -DI2PD_BUILD_TUI=OFF
cmake --build build-output --parallel "$JOBS" --target i2pd
strip build-output/i2pd
