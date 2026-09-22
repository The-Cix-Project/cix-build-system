#!/bin/sh
set -eu

cbs=${1:?usage: library-path-test.sh CBS}
temporary_dir=${TMPDIR:-/tmp}/cbs-library-path-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/image/lib" "$temporary_dir/staged"
printf '%s\n' 'test library' >"$temporary_dir/image/lib/libpolicy.so"

recipe=$temporary_dir/policy.cbs
cat >"$recipe" <<'EOF'
package "library-path" {
    version "1"
    release 1
    format "cixpkg"
    build {
        stage library "libpolicy.so" into "${dest}/usr/lib"
    }
}
EOF

"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/staged" \
    --library-path "$temporary_dir/image/lib"
test -f "$temporary_dir/staged/dest/usr/lib/libpolicy.so"
cmp -s "$temporary_dir/image/lib/libpolicy.so" \
    "$temporary_dir/staged/dest/usr/lib/libpolicy.so"

set +e
"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/rejected" \
    --library-path ":/usr/lib" >"$temporary_dir/rejected.out" \
    2>"$temporary_dir/rejected.err"
status=$?
set -e
test "$status" -eq 2
grep -q 'must contain only non-empty absolute directories' \
    "$temporary_dir/rejected.err"

echo "library path policy test: PASS"
