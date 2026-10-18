# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# POSIX make wrapper: tests are defined and built by CMake/CTest.

BUILD_DIR ?= ../build-output
CMAKE ?= cmake

all:
	ctest --test-dir ${BUILD_DIR} --output-on-failure

clean:
	${CMAKE} -E remove_directory ${BUILD_DIR}
