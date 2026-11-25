# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: CMakeLists.txt.
#
# Android CMake wrapper that enables the in-process JNI build with
# Torrents and UPnP while disabling host binaries, libraries, tools, TUI
# and tests.
#

# Android app entry point. The router itself is built by the main CMake
# graph; this wrapper only enables the JNI target and disables host-only
# components.
cmake_minimum_required(VERSION 3.24)
project(OpenI2PdAndroid LANGUAGES C CXX)

# The router service loads the JNI library in the app process. The SAF
# broker loads the same library in a separate process under the app
# UID. Android uses Binder for that broker and excludes the native
# i2pd_privsep target.
set(I2PD_BUILD_JNI ON CACHE BOOL "" FORCE)
set(WITH_BINARY OFF CACHE BOOL "" FORCE)
set(WITH_LIBRARY OFF CACHE BOOL "" FORCE)
set(I2PD_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(I2PD_BUILD_TUI OFF CACHE BOOL "" FORCE)
set(WITH_TORRENTS ON CACHE BOOL "" FORCE)
# UPnP is part of the Android product: it is enabled by default in the
# bundled configuration and backed by MiniUPnPc from the Android vcpkg
# manifest. Keep the daemon/UPnP.cpp and daemon/Daemon.cpp sources in
# the JNI target so this is a real feature rather than a
# configuration-only default.
set(WITH_UPNP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# The NDK toolchain restricts find_path/find_library/find_package to the
# sysroot by default. vcpkg prefixes (Boost, OpenSSL, zlib, MiniUPnPc)
# live outside it, so use the same BOTH modes as the manual native CI
# build. Keep the sysroot first in the search order.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../../build"
  "${CMAKE_BINARY_DIR}/i2pd")
