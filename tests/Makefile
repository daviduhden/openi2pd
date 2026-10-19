# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# POSIX make wrapper: tests are defined and built by CMake/CTest.

BUILD_DIR ?= ../build-output
CMAKE ?= cmake
CMAKE_GENERATOR ?= Ninja
CMAKE_FLAGS ?=

configure:
	"${CMAKE}" -S ../build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DBUILD_TESTING=ON ${CMAKE_FLAGS}

build: configure
	"${CMAKE}" --build "${BUILD_DIR}"

test: build
	ctest --test-dir "${BUILD_DIR}" --output-on-failure

all: test

check: test

clean:
	"${CMAKE}" -E remove_directory "${BUILD_DIR}"
