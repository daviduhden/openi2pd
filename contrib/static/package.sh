#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: package.
#
# Package prebuilt static i2pd binaries as Debian or RPM packages in a
# matching container. Usage: `package.sh deb|rpm BIN OUT`; set VERSION,
# RPM_VERSION and RPM_RELEASE in the environment.
#

# Run in a Debian or RPM build container with permission to install
# packaging tools. BIN contains i2pd-static-<Alpine-arch>/i2pd; OUT gets
# the resulting packages. VERSION is needed for Debian and output names;
# RPM_VERSION and RPM_RELEASE also select the RPM version and release.
# Architectures without an input binary are skipped, not treated as
# errors. This script leaves its mktemp workspace in place; the
# container or caller owns cleanup and must not assume every
# architecture was packaged.
#
# Requires root for apt-get/dnf and package tools, network access for
# those managers, and tools that target the requested architectures.
# `dpkg-buildpackage -d` skips Build-Depends checks because nothing is
# compiled; lintian runs only on amd64 Debian output and aborts on
# errors. Output names use linux-all plus the target architecture.
# Unsupported FORMAT prints usage and exits 1.
set -e

FORMAT=$1
BIN=$(realpath "$2")
mkdir -p "$3"
OUT=$(realpath "$3")
SRC=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
# Map the input binary's Alpine architecture to Debian and RPM names.
ARCHS='x86_64:amd64:x86_64
aarch64:arm64:aarch64
armhf:armhf:armv7hl
x86:i386:i686
s390x:s390x:s390x'
WORK=$(mktemp -d)

if [ "$FORMAT" = deb ]; then
	apt-get update -qq
	DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends debhelper dpkg-dev lintian >/dev/null
	PKG=$WORK/pkg
	mkdir -p "$PKG/bin"
	cp -r "$SRC/contrib/static/debian" "$PKG/debian"
	cat "$SRC/debian/copyright" "$PKG/debian/copyright-bundled" >"$PKG/debian/copyright"
	rm "$PKG/debian/copyright-bundled"
	cp "$SRC/contrib/i2pd.service" "$PKG/debian/i2pd.service"
	cp "$SRC/debian/i2pd.tmpfiles" "$PKG/debian/i2pd.tmpfiles"
	cp "$SRC/contrib/i2pd.logrotate" "$PKG/debian/i2pd.logrotate"
	cp "$SRC/debian/i2pd.init" "$PKG/debian/i2pd.init"
	cp "$SRC/debian/i2pd.default" "$PKG/debian/i2pd.default"
	ln -s "$SRC" "$PKG/src"
	printf 'i2pd (%s) stable; urgency=medium\n\n  * Static build.\n\n -- %s  %s\n' \
		"$VERSION" "$(sed -n 's/^Maintainer: //p' "$PKG/debian/control")" "$(date -R)" >"$PKG/debian/changelog"
	while IFS= read -r a; do
		[ -n "$a" ] || continue
		alpine=${a%%:*}
		rest=${a#*:}
		deb=${rest%%:*}
		[ -f "$BIN/i2pd-static-$alpine/i2pd" ] || continue
		install -m 755 "$BIN/i2pd-static-$alpine/i2pd" "$PKG/bin/i2pd-$deb"
		(cd "$PKG" && dpkg-buildpackage -b -us -uc -a"$deb" -d)
		mv "$WORK/i2pd_${VERSION}_${deb}.deb" "$OUT/i2pd_${VERSION}_linux-all_$deb.deb"
	done <<EOF
$ARCHS
EOF
	if [ -f "$OUT/i2pd_${VERSION}_linux-all_amd64.deb" ]; then
		lintian --fail-on error "$OUT/i2pd_${VERSION}_linux-all_amd64.deb"
	fi
elif [ "$FORMAT" = rpm ]; then
	dnf -q -y install rpm-build >/dev/null
	mkdir -p "$WORK/sources"
	while IFS= read -r a; do
		[ -n "$a" ] || continue
		alpine=${a%%:*}
		rpm=${a##*:}
		[ -f "$BIN/i2pd-static-$alpine/i2pd" ] || continue
		install -m 755 "$BIN/i2pd-static-$alpine/i2pd" "$WORK/sources/i2pd-$rpm"
		rpmbuild -bb --target "$rpm" -D "ver $RPM_VERSION" -D "rel $RPM_RELEASE" -D "srcdir $SRC" \
			-D "_topdir $WORK/rpmbuild" -D "_sourcedir $WORK/sources" -D "_rpmdir $WORK/rpms" "$SRC/contrib/static/i2pd.spec"
		mv "$WORK/rpms/$rpm/i2pd-$RPM_VERSION-$RPM_RELEASE.$rpm.rpm" "$OUT/i2pd_${VERSION}_linux-all_$rpm.rpm"
	done <<EOF
$ARCHS
EOF
else
	echo "usage: $0 deb|rpm BIN OUT" >&2
	exit 1
fi
