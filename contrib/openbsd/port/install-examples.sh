#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: OpenBSD port installation driver.
#
# Arguments: PHASE WRKDIST PREFIX WRKINST PORT_DIRECTORY COMMAND [ARGS...].
# Each phase receives the framework's INSTALL_* command as actual argv,
# preserving its flags/wrappers without eval or string-to-command parsing.
# The port supplies the substituted rc.d script in WRKDIST; pkg data comes
# from the external PORT_DIRECTORY. This script owns no build or services.

set -eu
if [ "$#" -lt 6 ]; then
	printf '%s\n' 'usage: install-examples.sh dirs|data|script WRKDIST PREFIX WRKINST PORT_DIRECTORY COMMAND [ARGS...]' >&2
	exit 2
fi
phase=$1
source_root=$2
prefix=$3
work_install=$4
port_directory=$5
shift 5

case "$phase" in
dirs)
	"$@" "$prefix/share/examples/etc/i2pd" \
		"$prefix/share/examples/etc/i2pd/tunnels.conf.d" \
		"$prefix/share/examples/etc/login.conf.d" \
		"$prefix/share/examples/var/i2pd" "$prefix/share/doc/i2pd"
	"$@" "$work_install/etc/rc.d"
	;;
data)
	"$@" "$source_root/contrib/openbsd/i2pd.conf" "$prefix/share/examples/etc/i2pd/i2pd.conf"
	"$@" "$source_root/contrib/tunnels.conf" "$prefix/share/examples/etc/i2pd/tunnels.conf"
	"$@" "$source_root/contrib/tunnels.d/README" "$prefix/share/examples/etc/i2pd/tunnels.conf.d/README"
	"$@" "$source_root/contrib/openbsd/i2pd.login" "$prefix/share/examples/etc/login.conf.d/i2pd"
	"$@" "$port_directory/pkg/var-i2pd.README" "$prefix/share/examples/var/i2pd/README"
	"$@" "$source_root/contrib/subscriptions.txt" "$prefix/share/examples/var/i2pd/subscriptions.txt"
	"$@" "$source_root/LICENSE" "$prefix/share/doc/i2pd/LICENSE"
	"$@" "$source_root/NOTICE" "$prefix/share/doc/i2pd/NOTICE"
	"$@" "$source_root/tools/LICENSE" "$prefix/share/doc/i2pd/LICENSE.i2pd-tools"
	;;
script)
	"$@" "$source_root/contrib/openbsd/i2pd.rc" "$work_install/etc/rc.d/i2pd"
	;;
*)
	printf 'unknown OpenBSD install phase: %s\n' "$phase" >&2
	exit 2
	;;
esac
