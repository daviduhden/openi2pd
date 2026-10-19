# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Makefile.
#
# Convenience targets for the router, TUI, tools, tests, installation and
# source distribution; CMake/Ninja provide the actual build rules.
#

# Portable POSIX make front-end for the CMake build.
#
# This file intentionally uses only POSIX make syntax: no GNU make functions,
# pattern substitutions, conditionals or other extensions. It is usable with
# BSD make on OpenBSD. CMake is the source of build rules.
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

BUILD_DIR = build-output
CMAKE = cmake
CMAKE_GENERATOR = Ninja
CMAKE_FLAGS =

configure:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" ${CMAKE_FLAGS}

build: configure
	"${CMAKE}" --build "${BUILD_DIR}"

all: build

router: configure
	"${CMAKE}" --build "${BUILD_DIR}" --target i2pd

tui:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DI2PD_BUILD_TUI=ON ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}" --target i2pd-tui

tools:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DI2PD_BUILD_TOOLS=ON ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}"

test:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DBUILD_TESTING=ON ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}"
	ctest --test-dir "${BUILD_DIR}" --output-on-failure

check: test

install: configure
	"${CMAKE}" --install "${BUILD_DIR}"

clean:
	"${CMAKE}" -E remove_directory "${BUILD_DIR}"

dist:
	git archive --format=tar.gz -9 --prefix=openi2pd/ HEAD -o openi2pd.tar.gz
