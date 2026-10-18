# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# Portable POSIX make front-end for the CMake build.
#
# This file intentionally uses only POSIX make syntax: no wildcard(), shell(),
# patsubst(), ifeq or other GNU extensions. It is usable with BSD make on
# OpenBSD. CMake is the source of build rules.
#
# The default generator is Ninja because CMake's "Unix Makefiles" generator
# emits GNU make syntax. Ninja is therefore required by this convenience
# front-end; invoking CMake directly remains possible with any generator that
# matches the local make implementation.
#
#   make
#   make BUILD_DIR=build-release CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"
#   make test
#   make install
#
# Parallelism is controlled with CMAKE_BUILD_PARALLEL_LEVEL, not make -j.

BUILD_DIR ?= build-output
CMAKE ?= cmake
CMAKE_GENERATOR ?= Ninja
CMAKE_FLAGS ?=

all:
	${CMAKE} -S build -B ${BUILD_DIR} -G ${CMAKE_GENERATOR} ${CMAKE_FLAGS}
	${CMAKE} --build ${BUILD_DIR}

router:
	${CMAKE} -S build -B ${BUILD_DIR} -G ${CMAKE_GENERATOR} ${CMAKE_FLAGS}
	${CMAKE} --build ${BUILD_DIR} --target i2pd

tui:
	${CMAKE} -S build -B ${BUILD_DIR} -G ${CMAKE_GENERATOR} -DWITH_TUI=ON ${CMAKE_FLAGS}
	${CMAKE} --build ${BUILD_DIR} --target i2pd-tui

test:
	${CMAKE} -S build -B ${BUILD_DIR} -G ${CMAKE_GENERATOR} -DBUILD_TESTING=ON ${CMAKE_FLAGS}
	${CMAKE} --build ${BUILD_DIR}
	ctest --test-dir ${BUILD_DIR} --output-on-failure

install:
	${CMAKE} --install ${BUILD_DIR}

clean:
	${CMAKE} -E remove_directory ${BUILD_DIR}

dist:
	git archive --format=tar.gz -9 --prefix=openi2pd/ HEAD -o openi2pd.tar.gz
