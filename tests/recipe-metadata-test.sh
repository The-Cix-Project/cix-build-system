#!/bin/bash
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
echo "recipe metadata test: PASS (pinned non-placeholder checksum)"
