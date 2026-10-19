#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: sync.
#
# Manual upstream comparison helper: it shallow-clones pinned i2pd_docs_en and
# i2pd-tools commits into a temporary directory and prints diffs against local
# files. It verifies documentation inventory coverage, never writes tracked
# files, and exits non-zero on unclassified or HTML upstream pages.
#

set -eu

DOCS_REPO=https://github.com/PurpleI2P/i2pd_docs_en
DOCS_COMMIT=30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e
TOOLS_REPO=https://github.com/PurpleI2P/i2pd-tools
TOOLS_COMMIT=ca7ece8da86aa80b58fce6fa64375bfd75cbf843

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMP=${TMPDIR:-/tmp}/openi2pd-upstream.$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP"

# A few upstream pages have a different local path to keep the fork tree
# navigable (currently the nested BitTorrent guide and RPC reference). The map
# is tab-separated: upstream path <TAB> local path.
resolve_docs_local() {
  upstream=$1
  mapped=
  if [ -f "$ROOT/contrib/upstream/docs-path-map.txt" ]; then
    mapped=$(
      while IFS='	' read -r source target; do
        [ -n "$source" ] || continue
        if [ "$source" = "$upstream" ]; then
          printf '%s\n' "$target"
          break
        fi
      done < "$ROOT/contrib/upstream/docs-path-map.txt"
    )
  fi
  if [ -n "$mapped" ]; then
    printf '%s\n' "$mapped"
  else
    printf '%s\n' "$upstream"
  fi
}

check_docs_coverage() {
  source_root=$1
  failed=0
  printf '\n== Upstream documentation coverage\n'
  find "$source_root/docs" -type f | sort | while IFS= read -r upstream_file; do
    relative=${upstream_file#"$source_root/docs/"}
    case "$relative" in
    *.html|*.htm)
      printf 'HTML SOURCE: %s (convert manually to Markdown before import)\n' "$relative"
      printf 'x\n' >> "$TMP/html-found"
      ;;
    esac
    if ! grep -Fxq "$relative" "$ROOT/contrib/upstream/docs-files.txt"; then
      printf 'UNTRACKED UPSTREAM: %s (add it to docs-files.txt or document why it is excluded)\n' "$relative"
      printf 'x\n' >> "$TMP/unclassified-found"
    fi
  done
  if [ -f "$TMP/html-found" ] || [ -f "$TMP/unclassified-found" ]; then
    failed=1
  fi
  if [ -f "$ROOT/contrib/upstream/docs-path-map.txt" ]; then
    while IFS='	' read -r source target; do
      [ -n "$source" ] || continue
      if ! grep -Fxq "$source" "$ROOT/contrib/upstream/docs-files.txt"; then
        printf 'STALE MAP SOURCE: %s (not in docs-files.txt)\n' "$source"
        failed=1
      fi
      if [ ! -f "$ROOT/docs/$target" ]; then
        printf 'MISSING MAP TARGET: %s -> docs/%s\n' "$source" "$target"
        failed=1
      fi
    done < "$ROOT/contrib/upstream/docs-path-map.txt"
  fi
  while IFS= read -r upstream_file; do
    [ -n "$upstream_file" ] || continue
    local_file=$(resolve_docs_local "$upstream_file")
    if [ ! -f "$ROOT/docs/$local_file" ]; then
      printf 'MISSING LOCAL: %s -> docs/%s\n' "$upstream_file" "$local_file"
      failed=1
    fi
  done < "$ROOT/contrib/upstream/docs-files.txt"
  if [ "$failed" -eq 0 ]; then
    printf 'Coverage OK: every upstream file is tracked and has a local target.\n'
  fi
  return "$failed"
}

do_docs=${1:-}
if [ -z "$do_docs" ] || [ "$do_docs" = "--docs" ] || [ "$do_docs" = "--all" ]; then
  git clone --quiet --no-checkout "$DOCS_REPO" "$TMP/docs-src"
  git -C "$TMP/docs-src" fetch --quiet --depth 1 origin "$DOCS_COMMIT"
  git -C "$TMP/docs-src" checkout --quiet "$DOCS_COMMIT"
  echo "== i2pd_docs_en $DOCS_COMMIT"
  if ! check_docs_coverage "$TMP/docs-src"; then
    echo "Documentation coverage failed; convert HTML or classify new files before syncing." >&2
    exit 1
  fi
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    local_file=$(resolve_docs_local "$file")
    diff -u "$TMP/docs-src/docs/$file" "$ROOT/docs/$local_file" || true
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
