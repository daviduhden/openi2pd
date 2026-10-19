# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Makefile.
#
# POSIX make wrapper that configures the CMake build with BUILD_TESTING=ON,
# builds it and runs CTest.
#

BUILD_DIR = ../build-output
CMAKE = cmake
CMAKE_GENERATOR = Ninja
CMAKE_FLAGS =

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
