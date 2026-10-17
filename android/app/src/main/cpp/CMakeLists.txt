# Android app entry point. The router itself is built by the main CMake graph;
# this wrapper only enables the JNI target and disables host-only components.
cmake_minimum_required(VERSION 3.22)

set(I2PD_BUILD_JNI ON CACHE BOOL "" FORCE)
set(WITH_BINARY OFF CACHE BOOL "" FORCE)
set(WITH_LIBRARY OFF CACHE BOOL "" FORCE)
set(I2PD_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(I2PD_BUILD_TUI OFF CACHE BOOL "" FORCE)
set(WITH_TORRENTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../../build"
  "${CMAKE_BINARY_DIR}/i2pd")
