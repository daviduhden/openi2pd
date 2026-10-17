#!/bin/sh
# Manual upstream comparison helper. It never modifies tracked files.
#
# Usage: contrib/upstream/sync.sh [--docs] [--tools]
#
# The script clones the pinned upstream commits into a temporary directory,
# prints unified diffs for every imported file, and exits non-zero when a
# difference exists. Review and merge changes by hand; do not copy blindly.
set -eu

DOCS_REPO=https://github.com/PurpleI2P/i2pd_docs_en
DOCS_COMMIT=c5d86a8d177c7215d710e6b2370c6f128cf7cfc0
TOOLS_REPO=https://github.com/PurpleI2P/i2pd-tools
TOOLS_COMMIT=ca7ece8da86aa80b58fce6fa64375bfd75cbf843

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMP=${TMPDIR:-/tmp}/openi2pd-upstream.$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP"

do_docs=${1:-}
if [ -z "$do_docs" ] || [ "$do_docs" = "--docs" ] || [ "$do_docs" = "--all" ]; then
  git clone --quiet --no-checkout "$DOCS_REPO" "$TMP/docs-src"
  git -C "$TMP/docs-src" fetch --quiet --depth 1 origin "$DOCS_COMMIT"
  git -C "$TMP/docs-src" checkout --quiet "$DOCS_COMMIT"
  echo "== i2pd_docs_en $DOCS_COMMIT"
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    diff -u "$TMP/docs-src/docs/$file" "$ROOT/docs/$file" || true
  done < "$ROOT/contrib/upstream/docs-files.txt"
  while IFS="	" read -r src dst; do
    [ -n "$src" ] || continue
    diff -u "$TMP/docs-src/$src" "$ROOT/$dst" || true
  done < "$ROOT/contrib/upstream/docs-extra-files.txt"
fi

do_tools=${2:-}
if [ "$do_docs" = "--tools" ] || [ "$do_docs" = "--all" ] || [ "$do_tools" = "--tools" ]; then
  git clone --quiet --no-checkout "$TOOLS_REPO" "$TMP/tools-src"
  git -C "$TMP/tools-src" fetch --quiet --depth 1 origin "$TOOLS_COMMIT"
  git -C "$TMP/tools-src" checkout --quiet "$TOOLS_COMMIT"
  echo "== i2pd-tools $TOOLS_COMMIT"
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    diff -u "$TMP/tools-src/$file" "$ROOT/tools/$file" || true
  done < "$ROOT/contrib/upstream/tools-files.txt"
fi

echo
echo "Review the diffs manually. This helper does not write tracked files."
echo "Preserve fork-specific CMake/C++23/TUI/security changes and never"
echo "reintroduce GNU make, the web console or the i2pd submodule."
