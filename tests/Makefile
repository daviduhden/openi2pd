# POSIX make wrapper: tests are defined and built by CMake/CTest.

BUILD_DIR ?= ../build-output
CMAKE ?= cmake

all:
	ctest --test-dir ${BUILD_DIR} --output-on-failure

clean:
	${CMAKE} -E remove_directory ${BUILD_DIR}
