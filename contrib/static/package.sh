#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: package.
#
# Packages prebuilt static per-architecture i2pd binaries into Debian or RPM
# packages inside a matching container. Usage is `package.sh deb|rpm BIN OUT`,
# with VERSION, RPM_VERSION and RPM_RELEASE supplied through the environment.
#

# Packages the static binaries present in BIN/i2pd-static-<arch>/i2pd, run inside debian (deb) or rocky/fedora (rpm)
# usage: package.sh deb|rpm BIN OUT, with VERSION (deb), RPM_VERSION and RPM_RELEASE in the environment
set -e

FORMAT=$1
BIN=$(realpath "$2")
mkdir -p "$3"
OUT=$(realpath "$3")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
# alpine:deb:rpm architecture names
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
	cp "$SRC/debian/CC-BY-SA-4.0" "$PKG/debian/CC-BY-SA-4.0"
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
