# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Makefile.
#
# Compatibility shim for the historical tests/ entry point. The root POSIX
# Makefile is the single source of build configuration; this file only
# forwards the same targets so `make -C tests` keeps working without a second
# copy of the CMake options and build directory.
#

.PHONY: all configure build test check clean

all:
	"${MAKE}" -C ".." all

configure:
	"${MAKE}" -C ".." configure

build:
	"${MAKE}" -C ".." build

test:
	"${MAKE}" -C ".." test

check:
	"${MAKE}" -C ".." check

clean:
	"${MAKE}" -C ".." clean
