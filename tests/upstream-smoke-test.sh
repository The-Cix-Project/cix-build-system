#!/bin/sh
set -eu

cbs=${1:?usage: upstream-smoke-test.sh CBS}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
stage_dir=$(mktemp -d "${TMPDIR:-/tmp}/cbs-upstream-stage.XXXXXX")
cache_dir=$(mktemp -d "${TMPDIR:-/tmp}/cbs-upstream-cache.XXXXXX")
artifact=$(mktemp "${TMPDIR:-/tmp}/cbs-upstream-artifact.XXXXXX.cixpkg")
trap 'rm -rf -- "$stage_dir" "$cache_dir" "$artifact"' EXIT HUP INT TERM

"$cbs" build "$tests_dir/../recipes/tcc.cbs" \
    --arch x86_64 --staged "$stage_dir" --output "$artifact" \
    --cache "$cache_dir" >/dev/null
"$cbs" verify "$artifact" >/dev/null
test -x "$stage_dir/dest/usr/bin/tcc"

zstd_stage=$(mktemp -d "${TMPDIR:-/tmp}/cbs-zstd-stage.XXXXXX")
zstd_cache=$(mktemp -d "${TMPDIR:-/tmp}/cbs-zstd-cache.XXXXXX")
zstd_artifact=$(mktemp "${TMPDIR:-/tmp}/cbs-zstd-artifact.XXXXXX.cixpkg")
trap 'rm -rf -- "$stage_dir" "$cache_dir" "$artifact" "$zstd_stage" "$zstd_cache" "$zstd_artifact"' EXIT HUP INT TERM
"$cbs" build "$tests_dir/../recipes/zstd.cbs" \
    --arch x86_64 --staged "$zstd_stage" --output "$zstd_artifact" \
    --cache "$zstd_cache" >/dev/null
"$cbs" verify "$zstd_artifact" >/dev/null
test -f "$zstd_stage/dest/usr/lib/libzstd.a"
printf '%s\n' 'upstream smoke test: PASS (TCC bootstrap, zstd build, and verification)'
