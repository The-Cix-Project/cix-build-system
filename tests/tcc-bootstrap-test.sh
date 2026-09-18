#!/bin/sh
set -eu

cbs=${1:?usage: tcc-bootstrap-test.sh CBS}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
caller_cache=${2:-${CBS_UPSTREAM_CACHE:-}}
stage_dir=$(mktemp -d "${TMPDIR:-/tmp}/cbs-tcc-stage.XXXXXX")
cache_dir=$(mktemp -d "${TMPDIR:-/tmp}/cbs-tcc-cache.XXXXXX")
artifact=$(mktemp "${TMPDIR:-/tmp}/cbs-tcc-artifact.XXXXXX.cixpkg")
trap 'rm -rf -- "$stage_dir" "$cache_dir" "$artifact"' EXIT HUP INT TERM

if [ -n "$caller_cache" ]; then
    if [ ! -d "$caller_cache" ]; then
        echo "upstream cache is not a directory: $caller_cache" >&2
        exit 2
    fi
    find "$caller_cache" -maxdepth 1 -type f -exec cp -- {} "$cache_dir"/ \;
fi

"$cbs" build "$tests_dir/../recipes/tcc.cbs" \
    --arch x86_64 --staged "$stage_dir" --output "$artifact" \
    --cache "$cache_dir" >/dev/null
"$cbs" verify "$artifact" >/dev/null
test -x "$stage_dir/dest/usr/bin/tcc"
printf '%s\n' 'TCC bootstrap test: PASS'
