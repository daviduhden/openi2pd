#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: install a prebuilt Debian payload.
#
# Called from the scratch package root with ARCH PACKAGE_ROOT. The
# enclosing package.sh copies this whole debian/ directory and supplies
# bin/ plus the read-only src/ link. No compilation or stripping occurs;
# all destinations remain below the explicitly supplied package root.

set -eu
if [ "$#" -ne 2 ] || [ -z "$1" ] || [ -z "$2" ]; then
	printf '%s\n' 'usage: install-payload.sh ARCH PACKAGE_ROOT' >&2
	exit 2
fi
architecture=$1
package_root=$2
install -D -m 0755 "bin/i2pd-$architecture" "$package_root/usr/bin/i2pd"
install -D -m 0644 src/contrib/i2pd.conf "$package_root/etc/i2pd/i2pd.conf"
install -D -m 0644 src/contrib/tunnels.conf "$package_root/etc/i2pd/tunnels.conf"
install -D -m 0644 src/contrib/subscriptions.txt "$package_root/etc/i2pd/subscriptions.txt"
install -D -m 0644 src/contrib/tunnels.d/README "$package_root/etc/i2pd/tunnels.conf.d/README"
install -d "$package_root/usr/share/i2pd"
cp -r src/contrib/certificates "$package_root/usr/share/i2pd/"
