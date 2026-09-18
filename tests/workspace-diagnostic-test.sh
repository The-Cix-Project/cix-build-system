#!/bin/sh
set -eu

cbs=${1:?usage: workspace-diagnostic-test.sh CBS}
temporary_dir=$(mktemp -d)
missing="$temporary_dir/missing/workspace"

if "$cbs" build tests/fixtures/standalone-smoke.cbs \
    --arch x86_64 --staged "$missing" \
    >"$temporary_dir/missing.out" 2>"$temporary_dir/missing.err"; then
    echo 'workspace diagnostics: missing workspace unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'workspace: cannot prepare ' "$temporary_dir/missing.err" >/dev/null
grep -F 'No such file or directory' "$temporary_dir/missing.err" >/dev/null

mkdir "$temporary_dir/format"
if "$cbs" build tests/fixtures/unsupported-format.cbs \
    --arch x86_64 --staged "$temporary_dir/format" \
    >"$temporary_dir/format.out" 2>"$temporary_dir/format.err"; then
    echo 'format diagnostics: unsupported format unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'format: standalone builds require cixpkg' "$temporary_dir/format.err" >/dev/null

echo 'workspace diagnostics: PASS (workspace and format rejection stages)'
