#!/bin/sh
set -eu

cbs=${1:?usage: upstream-smoke-test.sh CBS}
caller_cache=${2:-${CBS_UPSTREAM_CACHE:-}}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
stage_dir=$(mktemp -d "${TMPDIR:-/tmp}/cbs-upstream-stage.XXXXXX")
cache_dir=$(mktemp -d "${TMPDIR:-/tmp}/cbs-upstream-cache.XXXXXX")
artifact=$(mktemp "${TMPDIR:-/tmp}/cbs-upstream-artifact.XXXXXX.cixpkg")
trap 'rm -rf -- "$stage_dir" "$cache_dir" "$artifact"' EXIT HUP INT TERM

if [ -n "$caller_cache" ]; then
	if [ ! -d "$caller_cache" ]; then
		echo "upstream cache is not a directory: $caller_cache" >&2
		exit 2
	fi
	find "$caller_cache" -maxdepth 1 -type f -exec cp -- {} "$cache_dir"/ \;
else
	echo 'upstream smoke test: SKIP (set CBS_UPSTREAM_CACHE to a digest-keyed source cache)' >&2
	exit 0
fi

zstd_stage=$(mktemp -d "${TMPDIR:-/tmp}/cbs-zstd-stage.XXXXXX")
zstd_artifact=$(mktemp "${TMPDIR:-/tmp}/cbs-zstd-artifact.XXXXXX.cixpkg")
trap 'rm -rf -- "$stage_dir" "$cache_dir" "$artifact" "$zstd_stage" "$zstd_artifact"' EXIT HUP INT TERM
"$cbs" build "$tests_dir/../recipes/zstd.cbs" \
    --arch x86_64 --staged "$zstd_stage" --output "$zstd_artifact" \
    --cache "$cache_dir" >/dev/null
"$cbs" verify "$zstd_artifact" >/dev/null
test -f "$zstd_stage/dest/usr/lib/libzstd.a"

zstd_repro_stage=$(mktemp -d "${TMPDIR:-/tmp}/cbs-zstd-repro-stage.XXXXXX")
zstd_repro_artifact=$(mktemp "${TMPDIR:-/tmp}/cbs-zstd-repro-artifact.XXXXXX.cixpkg")
trap 'rm -rf -- "$stage_dir" "$cache_dir" "$artifact" "$zstd_stage" "$zstd_artifact" "$zstd_repro_stage" "$zstd_repro_artifact"' EXIT HUP INT TERM
"$cbs" build "$tests_dir/../recipes/zstd.cbs" \
    --arch x86_64 --staged "$zstd_repro_stage" --output "$zstd_repro_artifact" \
    --cache "$cache_dir" >/dev/null
cmp -s "$zstd_artifact" "$zstd_repro_artifact"
printf '%s\n' 'upstream smoke test: PASS (zstd build, verification, and byte-identical rebuild)'
