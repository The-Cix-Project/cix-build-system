#!/bin/sh
set -eu
recipe=${1:?recipe path required}
line=$(grep 'sha256 ' "$recipe")
case "$line" in
  *aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa*)
    echo "recipe metadata test: placeholder checksum" >&2
    exit 1
    ;;
esac
hash=${line#*\"}; hash=${hash%%\"*}
case "$hash" in
  *[!0123456789abcdef]*)
    echo "recipe metadata test: checksum is not exactly 64 lowercase hex bytes" >&2
    exit 1
    ;;
esac
[ "${#hash}" -eq 64 ] || {
  echo "recipe metadata test: checksum is not exactly 64 lowercase hex bytes" >&2
  exit 1
}

# The pinned version and the archive URL must name the same release, and the
# pin must not run ahead of the tree that carries it: a release archive
# contains this recipe, so the pin can only ever point at an earlier or
# equal release.
version=$(sed -n 's/^ *version "\([^"]*\)".*/\1/p' "$recipe" | head -1)
url_tag=$(sed -n 's#.*/archive/v\([^"]*\)\.tar\.gz".*#\1#p' "$recipe" | head -1)
[ -n "$version" ] && [ -n "$url_tag" ] || {
  echo "recipe metadata test: cannot read pinned version or archive tag" >&2
  exit 1
}
[ "$version" = "$url_tag" ] || {
  echo "recipe metadata test: version $version does not match archive tag v$url_tag" >&2
  exit 1
}
tree_version=$(sed -n '1p' "$(dirname -- "$recipe")/VERSION")
newest=$(printf '%s\n%s\n' "$version" "$tree_version" | sort -V | tail -1)
[ "$newest" = "$tree_version" ] || {
  echo "recipe metadata test: pinned $version is newer than VERSION $tree_version" >&2
  exit 1
}
echo "recipe metadata test: PASS (pinned non-placeholder checksum; version $version matches its archive tag and does not exceed VERSION $tree_version)"
