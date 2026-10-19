#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check reference.
#
# Verifies that I2P_JAVA_REFERENCE is a clean checkout at tag i2p-2.13.0 and commit 9134f808337b401e8e53c73734c81fab04280c9d, warning when the reference tree is writable; it does not build or compare code.
#

set -eu

ref=${I2P_JAVA_REFERENCE:-}
if [ -z "$ref" ]; then
    echo "usage: I2P_JAVA_REFERENCE=/path/to/i2p-java-2.13.0 $0" >&2
    exit 2
fi
if [ ! -d "$ref/.git" ]; then
    echo "not a git checkout: $ref" >&2
    exit 1
fi

tag=$(git -C "$ref" describe --tags --exact-match 2>/dev/null || true)
commit=$(git -C "$ref" rev-parse HEAD)
[ "$tag" = "i2p-2.13.0" ] || {
    echo "unexpected tag: ${tag:-<detached>}" >&2
    exit 1
}
[ "$commit" = "9134f808337b401e8e53c73734c81fab04280c9d" ] || {
    echo "unexpected commit: $commit" >&2
    exit 1
}
if [ -n "$(git -C "$ref" status --porcelain)" ]; then
    echo "reference tree is not clean" >&2
    exit 1
fi
if [ -w "$ref" ]; then
    echo "warning: reference tree is writable; chmod -R a-w is recommended" >&2
fi
echo "I2P Java reference verified: $tag $commit"
