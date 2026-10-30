#!/bin/sh
set -eu

# `cbs fingerprint` exists so a key is knowable before a build.  That only
# holds for inputs the verb can be told about (#305).
cbs=${1:?usage: fingerprint-parity-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-fingerprint-parity.XXXXXX")
trap 'chmod -R u+w -- "$root" 2>/dev/null || true; rm -rf -- "$root"' \
    EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac
recipe=tests/fixtures/standalone-smoke.cbs

key_of() {
    "$absolute_cbs" list "$1" | sed -n 's/^metadata build_fingerprint //p'
}

# --firmware-root is bound by content.
mkdir "$root/fw" "$root/w1" "$root/w2"
printf 'v1' >"$root/fw/blob"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/w1" \
    --output "$root/one.cixpkg" --firmware-root "$root/fw" >/dev/null
one=$(key_of "$root/one.cixpkg")
printf '%s\n' "$one" | grep -Eq '^[0-9a-f]{64}$'
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        --firmware-root "$root/fw")" = "fingerprint $one"
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        "--firmware-root=$root/fw")" = "fingerprint $one"

# Changing the firmware content moves the build's key and the verb's together.
printf 'v2' >"$root/fw/blob"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/w2" \
    --output "$root/two.cixpkg" --firmware-root "$root/fw" >/dev/null
two=$(key_of "$root/two.cixpkg")
test "$one" != "$two"
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        --firmware-root "$root/fw")" = "fingerprint $two"

# --prune-policy contributes its three resolved booleans, so the verb has to
# load the file rather than name it.
mkdir "$root/w3" "$root/w4"
printf 'strip-debug\n' >"$root/policy-one"
printf 'strip-debug\ndrop-static-archives\n' >"$root/policy-two"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/w3" \
    --output "$root/p1.cixpkg" --prune-policy "$root/policy-one" >/dev/null
p1=$(key_of "$root/p1.cixpkg")
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        --prune-policy "$root/policy-one")" = "fingerprint $p1"
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        "--prune-policy=$root/policy-one")" = "fingerprint $p1"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/w4" \
    --output "$root/p2.cixpkg" --prune-policy "$root/policy-two" >/dev/null
p2=$(key_of "$root/p2.cixpkg")
test "$p1" != "$p2"
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        --prune-policy "$root/policy-two")" = "fingerprint $p2"
# A policy with no rules is not the same as no policy at all.
test "$p1" != "$(key_of "$root/one.cixpkg")"

# Both are refused the way a build refuses them, rather than ignored.
if "$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
    --firmware-root "$root/absent" >"$root/o" 2>"$root/e"; then
    echo 'fingerprint parity tests: an absent firmware root was accepted' >&2
    exit 1
fi
grep -F 'CBS-E1008' "$root/e" >/dev/null
printf 'not-a-rule\n' >"$root/policy-bad"
if "$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
    --prune-policy "$root/policy-bad" >"$root/o" 2>"$root/e"; then
    echo 'fingerprint parity tests: an invalid prune policy was accepted' >&2
    exit 1
fi
grep -F 'CBS-E1007' "$root/e" >/dev/null

echo 'fingerprint parity tests: PASS (firmware root and prune policy predict)'
