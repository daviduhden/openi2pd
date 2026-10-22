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
#   make DESTDIR=/tmp/stage PREFIX=/usr install
#
# Parallelism is controlled with CMAKE_BUILD_PARALLEL_LEVEL, not make -j.

BUILD_DIR = build-output
CMAKE = cmake
CMAKE_GENERATOR = Ninja
CMAKE_FLAGS =
DESTDIR =
PREFIX =

.PHONY: all configure build router tui tools test check install clean dist

all: build

configure:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" ${CMAKE_FLAGS}

build: configure
	"${CMAKE}" --build "${BUILD_DIR}"

router:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DI2PD_BUILD_TUI=OFF ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}" --target i2pd

tui:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DI2PD_BUILD_TUI=ON ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}" --target i2pd-tui

tools:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DI2PD_BUILD_TOOLS=ON -DI2PD_BUILD_TUI=OFF ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}"

test:
	"${CMAKE}" -S build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DBUILD_TESTING=ON -DI2PD_BUILD_TUI=OFF ${CMAKE_FLAGS}
	"${CMAKE}" --build "${BUILD_DIR}"
	ctest --test-dir "${BUILD_DIR}" --output-on-failure

check: test

install: build
	@if [ -n "${PREFIX}" ]; then \
		DESTDIR="${DESTDIR}" "${CMAKE}" --install "${BUILD_DIR}" --prefix "${PREFIX}"; \
	else \
		DESTDIR="${DESTDIR}" "${CMAKE}" --install "${BUILD_DIR}"; \
	fi

clean:
	@case "${BUILD_DIR}" in ""|"."|".."|"/") \
		echo "refusing to remove unsafe BUILD_DIR" >&2; exit 1;; \
	esac
	@if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then \
		"${CMAKE}" -E remove_directory "${BUILD_DIR}"; \
	else \
		echo "refusing to remove ${BUILD_DIR}: no CMakeCache.txt" >&2; exit 1; \
	fi

dist:
	git archive --format=tar.gz -9 --prefix=openi2pd/ HEAD -o openi2pd.tar.gz
