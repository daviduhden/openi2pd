#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Debian build command driver.
#
# Debhelper invokes ACTION from the source root. dpkg-buildflags observes
# the maintainer hardening variables exported by debian/rules. Each flag
# string is one CMake definition; no eval or secondary word splitting is
# permitted. DEB_BUILD_OPTIONS selects at most one parallel count.
# Installation receives the absolute source root as its second argument.
# This helper downloads nothing and performs only the requested phase.

set -eu
set -f

if [ "$#" -lt 1 ]; then
	printf '%s\n' 'usage: build-driver.sh configure|build|clean|install [SOURCE_ROOT]' >&2
	exit 2
fi

case "$1" in
configure)
	c_flags=$(dpkg-buildflags --get CFLAGS)
	cpp_flags=$(dpkg-buildflags --get CPPFLAGS)
	cxx_flags=$(dpkg-buildflags --get CXXFLAGS)
	link_flags=$(dpkg-buildflags --get LDFLAGS)
	cmake -S build -B build-debian -G Ninja \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DCMAKE_INSTALL_SYSCONFDIR=/etc \
		-DCMAKE_INSTALL_LOCALSTATEDIR=/var \
		-DCMAKE_INSTALL_RUNSTATEDIR=/run \
		-DI2PD_BUILD_TUI=ON -DI2PD_BUILD_TOOLS=ON \
		-DWITH_LIBRARY=OFF \
		"-DCMAKE_C_FLAGS=$c_flags $cpp_flags" \
		"-DCMAKE_CXX_FLAGS=$cxx_flags $cpp_flags" \
		"-DCMAKE_EXE_LINKER_FLAGS=$link_flags" \
		"-DCMAKE_SHARED_LINKER_FLAGS=$link_flags" \
		"-DCMAKE_MODULE_LINKER_FLAGS=$link_flags" \
		-DBUILD_TESTING=OFF -DI2PD_INSTALL_DOCS=ON \
		-DI2PD_INSTALL_CONFIG=ON -DI2PD_OPENSSL_EXPECT=OpenSSL \
		-DI2PD_SERVICE_MANAGER=none
	;;
build)
	jobs=
	seen_parallel=false
	for option in ${DEB_BUILD_OPTIONS-}; do
		case "$option" in
		parallel=*)
			if [ "$seen_parallel" = true ]; then
				printf '%s\n' 'multiple parallel counts in DEB_BUILD_OPTIONS' >&2
				exit 2
			fi
			seen_parallel=true
			jobs=${option#parallel=}
			case "$jobs" in
			*[!0-9]*)
				printf '%s\n' 'invalid parallel count in DEB_BUILD_OPTIONS' >&2
				exit 2
				;;
			esac
			if [ -n "$jobs" ]; then
				case "$jobs" in
				*[1-9]*) ;;
				*)
					printf '%s\n' 'parallel count must be positive' >&2
					exit 2
					;;
				esac
			fi
			;;
		esac
	done
	if [ -n "$jobs" ]; then
		cmake --build build-debian --parallel "$jobs"
	else
		cmake --build build-debian --parallel
	fi
	;;
clean)
	cmake -E remove_directory build-debian
	;;
install)
	if [ "$#" -ne 2 ] || [ -z "$2" ]; then
		printf '%s\n' 'install requires the source root' >&2
		exit 2
	fi
	DESTDIR=$2/debian/tmp cmake --install build-debian
	;;
*)
	printf 'unknown Debian build action: %s\n' "$1" >&2
	exit 2
	;;
esac
