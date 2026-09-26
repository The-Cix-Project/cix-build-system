#!/bin/sh
set -eu

cbs=${1:?usage: cli-finalize-test.sh CBS HELPER}
helper=${2:?usage: cli-finalize-test.sh CBS HELPER}
helper_dir=$(CDPATH= cd -- "$(dirname -- "$helper")" && pwd)
helper_name=$(basename -- "$helper")
temporary_dir=$(mktemp -d)
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir "$temporary_dir/workspace"

artifact="$temporary_dir/finalized.cixpkg"
"$cbs" build tests/fixtures/standalone-smoke.cbs \
    --arch x86_64 --staged "$temporary_dir/workspace" \
    --output "$artifact" --finalize-command "$helper_name" \
    --command-path "$helper_dir:/usr/bin"
"$cbs" extract "$artifact" --into "$temporary_dir/extracted" >/dev/null
test -f "$temporary_dir/extracted/finalized-by-embedder"

mkdir "$temporary_dir/workspace-fail"
if "$cbs" build tests/fixtures/standalone-smoke.cbs \
    --arch x86_64 --staged "$temporary_dir/workspace-fail" \
    --output "$temporary_dir/rejected.cixpkg" --finalize-command false \
    >"$temporary_dir/fail.out" 2>"$temporary_dir/fail.err"; then
    echo 'finalizer test: rejected finalizer unexpectedly succeeded' >&2
    exit 1
fi
test ! -e "$temporary_dir/rejected.cixpkg"
grep -q 'finalize: finalization policy rejected' "$temporary_dir/fail.err"

mkdir "$temporary_dir/workspace-policy"
if CBS_TEST_PRIVILEGED=1 "$cbs" build tests/fixtures/standalone-smoke.cbs \
    --arch x86_64 --staged "$temporary_dir/workspace-policy" \
    --output "$temporary_dir/policy-rejected.cixpkg" \
    --finalize-command "$helper_name" --command-path "$helper_dir:/usr/bin" \
    >"$temporary_dir/policy.out" 2>"$temporary_dir/policy.err"; then
    echo 'privileged staged file unexpectedly packaged' >&2
    exit 1
fi
grep -q 'error\[CPDL-E4007\].*manifest:.*setuid and setgid' \
    "$temporary_dir/policy.err"

echo 'CLI finalizer tests: PASS (mutation before manifest and fail-closed policy)'
