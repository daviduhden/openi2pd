#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: POSIX make command driver.
#
# Run from the repository root. The Makefile owns target dependencies;
# this helper owns command sequencing and cleanup/install decisions.
# Arguments: ACTION CMAKE BUILD_DIR GENERATOR DESTDIR PREFIX [FLAGS...].
# FLAGS arrive as argv, already quoted by the calling recipe. Never eval
# them or split them again. CMake/Ninja retain incremental build
# ownership; CMAKE_BUILD_PARALLEL_LEVEL passes through in the
# environment.

set -eu

if [ "$#" -lt 6 ]; then
	printf '%s\n' 'usage: make-driver.sh ACTION CMAKE BUILD_DIR GENERATOR DESTDIR PREFIX [FLAGS...]' >&2
	exit 2
fi
action=$1
cmake_command=$2
build_directory=$3
generator=$4
staging_directory=$5
install_prefix=$6
shift 6

configure()
{
	# Component defaults precede caller flags at each call site. A caller
	# can override a default through argv without any shell reparsing.
	"$cmake_command" -S build -B "$build_directory" -G "$generator" "$@"
}

case "$action" in
	configure)
		configure "$@"
		;;
	build)
		"$cmake_command" --build "$build_directory"
		;;
	router)
		configure -DI2PD_BUILD_TUI=OFF "$@"
		"$cmake_command" --build "$build_directory" --target i2pd
		;;
	tui)
		configure -DI2PD_BUILD_TUI=ON "$@"
		"$cmake_command" --build "$build_directory" --target i2pd-tui
		;;
	tools)
		configure -DI2PD_BUILD_TOOLS=ON -DI2PD_BUILD_TUI=OFF "$@"
		"$cmake_command" --build "$build_directory"
		;;
	test)
		configure -DBUILD_TESTING=ON -DI2PD_BUILD_TUI=OFF "$@"
		"$cmake_command" --build "$build_directory"
		ctest --test-dir "$build_directory" --output-on-failure
		;;
	install)
		# DESTDIR relocates the staging root; PREFIX changes the logical
		# install prefix. An empty PREFIX preserves the configured prefix.
		# This action does not build first; Make owns that prerequisite.
		if [ -n "$install_prefix" ]; then
			DESTDIR=$staging_directory "$cmake_command" --install "$build_directory" --prefix "$install_prefix"
		else
			DESTDIR=$staging_directory "$cmake_command" --install "$build_directory"
		fi
		;;
	clean)
		case "$build_directory" in
			''|.|..|/)
				printf '%s\n' 'refusing to remove unsafe BUILD_DIR' >&2
				exit 1
				;;
		esac
		# Validate physical ancestry, not only literal spellings. Prefix a
		# relative path so cd never interprets a leading '-' as an option.
		case "$build_directory" in
			/*) candidate_directory=$build_directory ;;
			*) candidate_directory=./$build_directory ;;
		esac
		if ! physical_directory=$(CDPATH='' cd -P "$candidate_directory" && pwd -P && printf '.'); then
			printf '%s\n' 'refusing to remove an inaccessible BUILD_DIR' >&2
			exit 1
		fi
		# The sentinel preserves trailing newlines in directory names.
		physical_directory=${physical_directory%.}
		physical_directory=${physical_directory%?}
		source_directory=$(pwd -P && printf '.')
		source_directory=${source_directory%.}
		source_directory=${source_directory%?}
		case "$physical_directory" in
			/) printf '%s\n' 'refusing to remove filesystem root' >&2; exit 1 ;;
		esac
		case "$source_directory/" in
			"$physical_directory/"*)
				printf '%s\n' 'refusing to remove source root or its ancestor' >&2
				exit 1
				;;
		esac
		if [ ! -f "$build_directory/CMakeCache.txt" ]; then
			printf 'refusing to remove %s: no CMakeCache.txt\n' "$build_directory" >&2
			exit 1
		fi
		# Keep the caller's operand. This pathname check is not a
		# descriptor-based deletion transaction; concurrent replacement
		# remains outside this administrative helper's guarantee.
		"$cmake_command" -E remove_directory "$build_directory"
		;;
	dist)
		# Archive the committed tree, not local edits or untracked files.
		# The output path is relative to the repository-root invocation.
		git archive --format=tar.gz -9 --prefix=openi2pd/ HEAD -o openi2pd.tar.gz
		;;
	*)
		printf 'unknown make driver action: %s\n' "$action" >&2
		exit 2
		;;
esac
