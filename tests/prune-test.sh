#!/bin/sh
set -eu

cbs=${1:?usage: prune-test.sh CBS}
root=${TMPDIR:-/tmp}/cbs-prune-tests.$$
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/stage-none" "$root/stage-policy"
cat >"$root/recipe.cbs" <<'EOF'
package "prune-contract" {
    version "1"
    release 1
    format "cixpkg"
    build {
        mkdir "${dest}/usr/lib" parents
        mkdir "${dest}/usr/bin" parents
        write "${build}/helper.c" "int main(void) { return 0; }\n"
        run "tcc" { "${build}/helper.c" "-g" "-o" "${dest}/usr/bin/helper" }
        write "${dest}/usr/lib/libfoo.a" "archive"
        write "${dest}/usr/lib/libfoo.so" "shared"
        write "${dest}/usr/lib/keep.a" "keep"
        write "${dest}/usr/lib/obsolete.la" "libtool"
    }
}
EOF
cat >"$root/policy" <<'EOF'
strip-debug
drop-static-archives
drop-libtool-archives
EOF
"$cbs" build "$root/recipe.cbs" --arch x86_64 --staged "$root/stage-none" \
    --output "$root/none.cixpkg"
"$cbs" build "$root/recipe.cbs" --arch x86_64 --staged "$root/stage-policy" \
    --output "$root/policy.cixpkg" --prune-policy "$root/policy" \
    --events=jsonl >"$root/build.out" 2>"$root/events.jsonl"
test "$(sha256sum "$root/none.cixpkg" | awk '{print $1}')" != \
    "$(sha256sum "$root/policy.cixpkg" | awk '{print $1}')"
grep -q '"type":"prune-remove"' "$root/events.jsonl"
grep -q '"type":"prune-modify"' "$root/events.jsonl"
grep -q 'drop-static-archives' "$root/events.jsonl"
grep -q 'drop-libtool-archives' "$root/events.jsonl"
"$cbs" extract "$root/none.cixpkg" --into "$root/none" >/dev/null
"$cbs" extract "$root/policy.cixpkg" --into "$root/policy-tree" >/dev/null
test -f "$root/none/usr/lib/libfoo.a"
test -f "$root/none/usr/lib/keep.a"
test -f "$root/none/usr/lib/obsolete.la"
test ! -e "$root/policy-tree/usr/lib/libfoo.a"
test -f "$root/policy-tree/usr/lib/keep.a"
test ! -e "$root/policy-tree/usr/lib/obsolete.la"
printf '%s\n' 'prune policy tests: PASS (declared removals, matching rules, and digest)'
