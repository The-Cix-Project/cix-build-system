#!/bin/sh
set -eu

cbs=${1:?usage: revise-test.sh CBS}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cbs-revise.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cp tests/fixtures/valid/complete.cbs "$tmp/input.cbs"
"$cbs" revise "$tmp/input.cbs" >"$tmp/identity.cbs"
cmp "$tmp/input.cbs" "$tmp/identity.cbs"

"$cbs" revise "$tmp/input.cbs" --set version=2.0 --set release=3 --output "$tmp/identity.cbs"
grep -q '^    version "2.0"$' "$tmp/identity.cbs"
grep -q '^    release 3$' "$tmp/identity.cbs"
"$cbs" check "$tmp/identity.cbs" >/dev/null

sha_a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
sha_b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
"$cbs" revise "$tmp/input.cbs" \
    --set source.main.url=https://example.invalid/main.tar \
    --set source.main.sha256="$sha_a" \
    --set source.extra.support.sha256="$sha_b" --output "$tmp/sources.cbs"
grep -q 'url "https://example.invalid/main.tar"' "$tmp/sources.cbs"
grep -q "sha256 \"$sha_a\"" "$tmp/sources.cbs"
"$cbs" check "$tmp/sources.cbs" >/dev/null

"$cbs" revise "$tmp/input.cbs" --set 'metadata.test=quoted "value"' --output "$tmp/metadata.cbs"
grep -q '^metadata {$' "$tmp/metadata.cbs"
grep -q '^    "test" "quoted \\"value\\""$' "$tmp/metadata.cbs"
"$cbs" check "$tmp/metadata.cbs" >/dev/null
"$cbs" revise "$tmp/metadata.cbs" --unset metadata.test >"$tmp/unset.cbs"
! grep -q '"test"' "$tmp/unset.cbs"
"$cbs" check "$tmp/unset.cbs" >/dev/null

if "$cbs" revise "$tmp/input.cbs" --set source.main.url=/usr/bin/cbs >"$tmp/rejected.cbs" 2>"$tmp/error"; then
    echo "missing target validation unexpectedly succeeded" >&2
    exit 1
fi
test ! -s "$tmp/rejected.cbs"

capabilities=$($cbs --capabilities)
echo "$capabilities" | grep -q '"revise"'
echo "revise tests: PASS"
