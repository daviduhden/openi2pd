# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Makefile.
#
# POSIX make wrapper that configures the CMake build with BUILD_TESTING=ON,
# builds it and runs CTest. Run this file from the tests/ directory (or with
# `make -C tests`), because its paths are relative to tests/.
#

BUILD_DIR = ../build-output
CMAKE = cmake
CMAKE_GENERATOR = Ninja
CMAKE_FLAGS =
DESTDIR =
PREFIX =

.PHONY: all configure build test check clean

all: test

configure:
	"${CMAKE}" -S ../build -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -DBUILD_TESTING=ON -DI2PD_BUILD_TUI=OFF ${CMAKE_FLAGS}

build: configure
	"${CMAKE}" --build "${BUILD_DIR}"

test: build
	ctest --test-dir "${BUILD_DIR}" --output-on-failure

check: test

clean:
	@case "${BUILD_DIR}" in ""|"."|".."|"/") \
		echo "refusing to remove unsafe BUILD_DIR" >&2; exit 1;; \
	esac
	@if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then \
		"${CMAKE}" -E remove_directory "${BUILD_DIR}"; \
	else \
		echo "refusing to remove ${BUILD_DIR}: no CMakeCache.txt" >&2; exit 1; \
	fi
