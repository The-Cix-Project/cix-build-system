#!/bin/sh
set -eu

# The manifest is line-oriented, so a value holding a line break ends its own
# `m` line and begins a forged one, in an artifact that still verifies (#316,
# the composer-side twin of #313).  A recipe's own values are refused by the
# validator, with fixtures for each; the values validation cannot see are the
# recipe PATH the composer passes to `cbs build`, which reaches `m recipe`, and
# the names of the staged files.  The single writer, cbs_manifest_line, and the
# check in the entry writer are the floor under all of them.
cbs=${1:?usage: manifest-line-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-manifest-line.XXXXXX")
trap 'rm -rf -- "${root:?}"' EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

recipe_text() {
    printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
        '    format "cixpkg"' '    build { write "${dest}/x" "x" }' '}'
}

# A clean path builds and records the recipe path as one line: the control.
mkdir "$root/clean" "$root/clean-ws"
recipe_text >"$root/clean/ok.cbs"
"$absolute_cbs" build "$root/clean/ok.cbs" --arch x86_64 \
    --staged "$root/clean-ws" --output "$root/clean.cixpkg" >/dev/null
"$absolute_cbs" list "$root/clean.cixpkg" >"$root/clean.list"
grep -q "^metadata recipe $root/clean/ok.cbs\$" "$root/clean.list" ||
    { echo 'manifest line: a clean recipe path was not recorded verbatim' >&2; exit 1; }

# A path with a line break must fail the build and write no artifact, and the
# line it would have forged must not appear anywhere.
mkdir "$root/hostile-ws"
hostile=$root/$(printf 'x\nm injected_by_path forged')y.cbs
recipe_text >"$hostile"
if "$absolute_cbs" build "$hostile" --arch x86_64 --staged "$root/hostile-ws" \
    --output "$root/hostile.cixpkg" >/dev/null 2>"$root/hostile.err"; then
    echo 'manifest line: a recipe path containing a line break was accepted' >&2
    exit 1
fi
[ ! -e "$root/hostile.cixpkg" ] ||
    { echo 'manifest line: an artifact was written for a hostile recipe path' >&2; exit 1; }
grep -q 'a recorded value contains a line break' "$root/hostile.err" ||
    { echo 'manifest line: the refusal does not name its cause' >&2; cat "$root/hostile.err" >&2; exit 1; }

# A staged path is the other value a recipe does not name outright: a directory
# called "d<LF>m forged yes" wrote `m forged yes` into the manifest of an artifact
# that still listed, and a file of that name wrote an artifact that failed to
# verify (#316).  Each must fail the build and write no artifact.  A name with an
# ordinary space is the control and must still build.
staged_case() {
    label=$1 operation=$2 expect=$3
    mkdir "$root/$label-ws"
    printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
        '    format "cixpkg"' "    build { $operation }" '}' >"$root/$label.cbs"
    if "$absolute_cbs" build "$root/$label.cbs" --arch x86_64 \
        --staged "$root/$label-ws" --output "$root/$label.cixpkg" \
        >/dev/null 2>"$root/$label.err"; then
        status=built
    else
        status=refused
    fi
    if [ "$status" != "$expect" ]; then
        echo "manifest line: $label: the build was $status, expected $expect" >&2
        exit 1
    fi
    if [ "$expect" = refused ]; then
        [ ! -e "$root/$label.cixpkg" ] ||
            { echo "manifest line: $label: an artifact was written" >&2; exit 1; }
        grep -q 'a staged path contains a line break' "$root/$label.err" ||
            { echo "manifest line: $label: the refusal does not name its cause" >&2; cat "$root/$label.err" >&2; exit 1; }
    else
        "$absolute_cbs" list "$root/$label.cixpkg" >"$root/$label.list"
        if grep -q forged "$root/$label.list"; then
            echo "manifest line: $label: a forged line is present" >&2
            exit 1
        fi
    fi
}
staged_case dir-lf 'mkdir "${dest}/d\nm forged yes"' refused
staged_case dir-cr 'mkdir "${dest}/d\rm forged yes"' refused
staged_case file-lf 'write "${dest}/a" "x" write "${dest}/a\nm forged yes" "x"' refused
staged_case space 'mkdir "${dest}/usr" write "${dest}/usr/a b" "x"' built

echo 'manifest line tests: PASS (a recipe path or a staged path with a line break fails the build and writes no artifact)'
