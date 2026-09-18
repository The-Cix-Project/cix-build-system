#!/bin/sh
set -eu

cbs=${1:?usage: context-contract-test.sh CBS}
root=${TMPDIR:-/tmp}/cbs-context-tests.$$
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/firmware" "$root/stage"
cat >"$root/recipe.cbs" <<'EOF'
package "context-contract" {
    version "1"
    release 1
    format "cixpkg"
    capability "kernel.firmware-root"
    capability "kernel.kconfig-merge"
    build {
        mkdir "${dest}"
        write "${dest}/marker" "ok"
        run "true" { "${firmware}" }
    }
}
EOF
set +e
"$cbs" build "$root/recipe.cbs" --arch x86_64 --staged "$root/stage" \
    --output "$root/missing.cixpkg" >"$root/missing.out" 2>"$root/missing.err"
status=$?
set -e
test "$status" -ne 0
grep -q 'no firmware root was supplied' "$root/missing.err"
"$cbs" build "$root/recipe.cbs" --arch x86_64 --staged "$root/stage" \
    --output "$root/context.cixpkg" --firmware-root "$root/firmware" \
    >/dev/null
printf '%s\n' 'executor context tests: PASS (firmware binding and capability declarations)'
