# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Makefile.
#
# Convenience targets for router, TUI, tools, tests, install and source
# distribution. CMake/Ninja provide build rules.
#

# Portable POSIX make front-end for the CMake build.
#
# This file uses only POSIX make syntax: no GNU make functions, pattern
# substitutions, conditionals or extensions. BSD make can use it on
# OpenBSD. CMake remains the source of build rules.
#
# The default generator is Ninja because CMake's "Unix Makefiles" emits
# GNU make syntax. This front-end requires Ninja. Direct CMake use can
# select any generator supported by the local build environment.
#
#   make
#   make BUILD_DIR=build-release
#   make CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"
#   make test
#   make DESTDIR=/tmp/stage PREFIX=/usr install
#
# Set parallelism with CMAKE_BUILD_PARALLEL_LEVEL, not make -j.

BUILD_DIR = build-output
CMAKE = cmake
CMAKE_GENERATOR = Ninja
CMAKE_FLAGS =
DESTDIR =
PREFIX =
MAKE_DRIVER = sh build/make-driver.sh
MAKE_DRIVER_ARGS = "${CMAKE}" "${BUILD_DIR}" "${CMAKE_GENERATOR}" "${DESTDIR}" "${PREFIX}"

.PHONY: all configure build router tui tools test check install clean dist

all: build

configure:
	${MAKE_DRIVER} configure ${MAKE_DRIVER_ARGS} ${CMAKE_FLAGS}

build: configure
	${MAKE_DRIVER} build ${MAKE_DRIVER_ARGS}

router:
	${MAKE_DRIVER} router ${MAKE_DRIVER_ARGS} ${CMAKE_FLAGS}

tui:
	${MAKE_DRIVER} tui ${MAKE_DRIVER_ARGS} ${CMAKE_FLAGS}

tools:
	${MAKE_DRIVER} tools ${MAKE_DRIVER_ARGS} ${CMAKE_FLAGS}

test:
	${MAKE_DRIVER} test ${MAKE_DRIVER_ARGS} ${CMAKE_FLAGS}

check: test

install: build
	@${MAKE_DRIVER} install ${MAKE_DRIVER_ARGS}

clean:
	@${MAKE_DRIVER} clean ${MAKE_DRIVER_ARGS}

dist:
	${MAKE_DRIVER} dist ${MAKE_DRIVER_ARGS}
