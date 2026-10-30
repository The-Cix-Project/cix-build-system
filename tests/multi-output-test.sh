#!/bin/sh
set -eu

# ADR-0038: one recipe, one build, several packages.
cbs=${1:?usage: multi-output-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-multi-output.XXXXXX")
trap 'chmod -R u+w -- "$root" 2>/dev/null || true; rm -rf -- "$root"' \
    EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

mkdir "$root/stage" "$root/artifacts"
"$absolute_cbs" build tests/fixtures/multi-output.cbs \
    --arch x86_64 --staged "$root/stage" --output-dir "$root/artifacts" \
    --events-file "$root/events" --report "$root/report.json" \
    >"$root/out" 2>"$root/err"

# Three artifacts, each canonically named: the two declared outputs and the
# main package holding what neither claimed.
test -f "$root/artifacts/cixctl-1.4.0-2-x86_64.cixpkg"
test -f "$root/artifacts/cix-tools-1.4.0-2-x86_64.cixpkg"
test -f "$root/artifacts/cix-1.4.0-2-x86_64.cixpkg"
test "$(ls "$root/artifacts" | wc -l)" -eq 3

# The partition: a claimed path appears in exactly one artifact.
"$absolute_cbs" list "$root/artifacts/cixctl-1.4.0-2-x86_64.cixpkg" >"$root/l1"
"$absolute_cbs" list "$root/artifacts/cix-tools-1.4.0-2-x86_64.cixpkg" >"$root/l2"
"$absolute_cbs" list "$root/artifacts/cix-1.4.0-2-x86_64.cixpkg" >"$root/l3"
grep -q ' usr/bin/cixctl$' "$root/l1"
grep -q ' usr/bin/mkbootroot$' "$root/l2"
grep -q ' usr/bin/mkinstalleriso$' "$root/l2"
grep -q ' usr/bin/cixd$' "$root/l3"
# cixd stayed out of the carved outputs, and cixctl out of the remainder.
if grep -q 'cixd' "$root/l1" || grep -q 'cixctl' "$root/l3"; then
    echo 'multi-output tests: a path reached the wrong artifact' >&2
    exit 1
fi
# A per-output license lands in that output's manifest and nowhere else: the
# output that declared none must not inherit it, nor must the main package.
grep -q 'metadata license LGPL-3.0-or-later' "$root/l1"
if grep -q 'metadata license' "$root/l2" || grep -q 'metadata license' "$root/l3"
then
    echo "multi-output tests: an output's license leaked into another" >&2
    exit 1
fi
# Each output carries its own requires and no other's, in the same shape the
# top-level `requires` uses (ADR-0038 4).  The artifact does not record
# requires, so read the plan.  `explain --json` is one line, so count
# occurrences with grep -o rather than matching lines.
"$absolute_cbs" explain tests/fixtures/multi-output.cbs --json >"$root/explain"
grep -q '"name":"cixctl","main":false,"files":\[{"pattern":"usr/bin/cixctl","glob":false}\],"requires":{"runtime":{"library":\["libcurl"\]}}}' \
    "$root/explain"
# The main package reports the package-level requires: a consumer iterating
# `outputs` must not read it as having no dependencies.
grep -q '"name":"cix","main":true,"files":\[\],"requires":{"runtime":{"library":\["libarchive"\]}}}' \
    "$root/explain"
# libcurl belongs to one output and no other.
test "$(grep -o '"libcurl"' "$root/explain" | wc -l)" -eq 1
# An output that declares none gets an empty object, not a null: one shape.
grep -q '"name":"cix-tools","main":false,.*"requires":{}}' "$root/explain"

# Every artifact verifies on its own, and a carved one extracts standalone:
# CIXPKG creates implicit parents, so directory entries are not duplicated.
for artifact in "$root/artifacts"/*.cixpkg; do
    "$absolute_cbs" verify "$artifact" >/dev/null
done
"$absolute_cbs" extract "$root/artifacts/cixctl-1.4.0-2-x86_64.cixpkg" \
    --into "$root/extracted" >/dev/null
test -f "$root/extracted/usr/bin/cixctl"

# One artifact-finalized per output, and the report summarises rather than
# naming one of three as though it were all of them (ADR-0038 6).
test "$(grep -c '"type":"artifact-finalized"' "$root/events")" -eq 3
grep -q '"output_count":3' "$root/report.json"
grep -q '"artifact":""' "$root/report.json"
# One build action, so every artifact carries the same provenance: the same
# recipe digest, the same engine and target, and the same fingerprint line.
# Only the per-output license and the manifest digest may differ.
for list in "$root/l1" "$root/l2" "$root/l3"; do
    grep '^metadata ' "$list" | grep -v '^metadata license ' >"$list.prov"
done
cmp -s "$root/l1.prov" "$root/l2.prov"
cmp -s "$root/l1.prov" "$root/l3.prov"
grep -q 'metadata recipe_sha256 ' "$root/l1.prov"
# Whatever the fingerprint is, every output carries the same one: the cmp
# above is the assertion.
grep -q 'metadata build_fingerprint ' "$root/l1.prov"
# The partition really split the tree: three different manifests.
test "$(grep -h '^manifest-digest ' "$root/l1" "$root/l2" "$root/l3" |
        sort -u | wc -l)" -eq 3

# Determinism: a second build of the same recipe is byte-identical per output.
mkdir "$root/stage2" "$root/artifacts2"
"$absolute_cbs" build tests/fixtures/multi-output.cbs \
    --arch x86_64 --staged "$root/stage2" --output-dir "$root/artifacts2" \
    >"$root/out2" 2>"$root/err2"
for artifact in "$root/artifacts"/*.cixpkg; do
    cmp -s "$artifact" "$root/artifacts2/$(basename "$artifact")"
done

# A single path cannot name several artifacts.
mkdir "$root/stage3"
if "$absolute_cbs" build tests/fixtures/multi-output.cbs \
    --arch x86_64 --staged "$root/stage3" --output "$root/one.cixpkg" \
    >"$root/out3" 2>"$root/err3"; then
    echo 'multi-output tests: --output accepted several outputs' >&2
    exit 1
fi
grep -q 'use --output-dir' "$root/err3"
test ! -f "$root/one.cixpkg"

# Two outputs claiming one path is refused naming both, and no artifact is
# left behind: all outputs or none.
mkdir "$root/stage4" "$root/artifacts4"
if "$absolute_cbs" build tests/fixtures/output-overlap.cbs \
    --arch x86_64 --staged "$root/stage4" --output-dir "$root/artifacts4" \
    >"$root/out4" 2>"$root/err4"; then
    echo 'multi-output tests: overlapping outputs were accepted' >&2
    exit 1
fi
grep -q 'claimed by outputs a and b' "$root/err4"
test "$(ls "$root/artifacts4" | wc -l)" -eq 0

echo 'multi-output tests: PASS (partition, per-output license and requires, canonical names, shared provenance, determinism, and the overlap and single-path refusals)'
