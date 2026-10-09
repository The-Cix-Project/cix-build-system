#!/bin/sh
set -eu

cbs=${1:?usage: workspace-diagnostic-test.sh CBS}
temporary_dir=$(mktemp -d)
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
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
if "$cbs" build tests/fixtures/invalid/unsupported-format.cbs \
    --arch x86_64 --staged "$temporary_dir/format" \
    >"$temporary_dir/format.out" 2>"$temporary_dir/format.err"; then
    echo 'format diagnostics: unsupported format unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'error[CPDL-E3004]: validation: artifact format must be cixpkg' \
    "$temporary_dir/format.err" >/dev/null

# Issue #284: a relative --staged is resolved once, so confined operations no
# longer fail with a confinement error naming a path that is inside the roots.
# The build runs from the workspace's parent so --staged is genuinely relative.
mkdir "$temporary_dir/relative"
repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
recipe="$repository/tests/fixtures/standalone-smoke.cbs"
# The build runs from another directory, so the binary needs an absolute path
# of its own: the caller may well have passed ./cbs.
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac
(
    cd "$temporary_dir/relative"
    mkdir ws abs
    "$absolute_cbs" build "$recipe" --arch x86_64 --staged ws \
        --output relative.cixpkg >relative.out 2>relative.err
    "$absolute_cbs" build "$recipe" --arch x86_64 --staged "$PWD/abs" \
        --output absolute.cixpkg >absolute.out 2>absolute.err
)
# mkdir, write and require all resolve against the roots, so the staged file
# proves confinement accepted a relative workspace rather than merely not
# crashing.
test -f "$temporary_dir/relative/ws/dest/usr/bin/hello"
"$cbs" verify "$temporary_dir/relative/relative.cixpkg" >/dev/null
# A workspace path must not reach the artifact: the two builds differ only in
# how the workspace was spelled.
cmp -s "$temporary_dir/relative/relative.cixpkg" \
    "$temporary_dir/relative/absolute.cixpkg"

echo 'workspace diagnostics: PASS (workspace and format rejection stages, relative workspace)'
