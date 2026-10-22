# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: CMakeLists.txt.
#
# Android CMake wrapper that enables the in-process JNI build with Torrents and
# UPnP while disabling host binaries, libraries, tools, TUI and tests.
#

# Android app entry point. The router itself is built by the main CMake graph;
# this wrapper only enables the JNI target and disables host-only components.
cmake_minimum_required(VERSION 3.24)

# Android intentionally builds only the in-process JNI library. i2pd_privsep
# is not linked here: the current Android service is same-process and the
# network/storage/crypto workers do not yet exist. Do not describe this as OS
# process separation.
set(I2PD_BUILD_JNI ON CACHE BOOL "" FORCE)
set(WITH_BINARY OFF CACHE BOOL "" FORCE)
set(WITH_LIBRARY OFF CACHE BOOL "" FORCE)
set(I2PD_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(I2PD_BUILD_TUI OFF CACHE BOOL "" FORCE)
set(WITH_TORRENTS ON CACHE BOOL "" FORCE)
# UPnP is part of the Android product: it is enabled by default in the
# bundled configuration and backed by MiniUPnPc from the Android vcpkg
# manifest. Keep the daemon/UPnP.cpp and daemon/Daemon.cpp sources in the JNI
# target so this is a real feature rather than a configuration-only default.
set(WITH_UPNP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../../build"
  "${CMAKE_BINARY_DIR}/i2pd")
