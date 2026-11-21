#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check reference.
#
# Verifies the current I2P Java 2.14.0 target checkout; historical
# fixture provenance remains pinned separately by check-vectors.pl. Does
# not build.
#
# Contract: I2P_JAVA_REFERENCE must name the root of a read-only
# i2p-2.14.0 git checkout, because this script only inspects git
# metadata. It exits 2 when the variable is missing or empty (usage on
# stderr), 1 for a non-repository, a different tag/commit/annotated tag
# object or a dirty worktree, and 0 when the expected refs match. It
# needs git but no network, compiler or test runner, and it never writes
# into the reference tree. A writable reference checkout is reported as
# a warning, not an error.
#

set -eu

ref=${I2P_JAVA_REFERENCE:-}
if [ -z "$ref" ]; then
	echo "usage: I2P_JAVA_REFERENCE=/path/to/i2p-java-2.14.0 $0" >&2
	exit 2
fi
if [ ! -d "$ref/.git" ]; then
	echo "not a git checkout: $ref" >&2
	exit 1
fi

read_git()
{
	git --no-replace-objects --no-optional-locks -C "$ref" "$@"
}

tag=i2p-2.14.0
commit=$(read_git rev-parse --verify HEAD) || exit 1
tag_commit=$(read_git rev-parse --verify "refs/tags/$tag^{commit}") || exit 1
[ "$tag_commit" = "$commit" ] || {
	echo "HEAD does not match the reference tag $tag" >&2
	exit 1
}
[ "$commit" = "3c98503c5f14efe7bdd783528ba7f621f12d8081" ] || {
	echo "unexpected commit: $commit" >&2
	exit 1
}
tag_object=$(read_git rev-parse --verify "refs/tags/$tag") || exit 1
tag_type=$(read_git cat-file -t "$tag_object") || exit 1
[ "$tag_type" = "tag" ] && [ "$tag_object" = "9db233ed7241599eba76f29b8c334eaaf3db2e87" ] || {
	echo "unexpected annotated tag object: ${tag_object:-<missing>} (${tag_type:-<unknown>})" >&2
	exit 1
}
status=$(read_git status --porcelain --untracked-files=all) || exit 1
if [ -n "$status" ]; then
	echo "reference tree is not clean" >&2
	exit 1
fi
if [ -w "$ref" ]; then
	echo "warning: reference tree is writable; chmod -R a-w is recommended" >&2
fi
echo "I2P Java reference verified: $tag $commit"
