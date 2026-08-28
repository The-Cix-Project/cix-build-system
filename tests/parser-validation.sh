#!/bin/sh
set -eu

cbs=${1:?usage: parser-validation.sh CBS}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temporary_dir=${TMPDIR:-/tmp}/cbs-parser-tests.$$
passed=0

cleanup()
{
    rm -rf -- "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM
mkdir -p -- "$temporary_dir"

for fixture in "$tests_dir"/fixtures/valid/*.cbs; do
    name=${fixture##*/}
    if ! "$cbs" validate "$fixture" >"$temporary_dir/$name.out" 2>"$temporary_dir/$name.err"; then
        echo "FAIL: expected valid: $name" >&2
        cat "$temporary_dir/$name.err" >&2
        exit 1
    fi
    if [ -s "$temporary_dir/$name.err" ]; then
        echo "FAIL: valid fixture emitted diagnostics: $name" >&2
        cat "$temporary_dir/$name.err" >&2
        exit 1
    fi
    passed=$((passed + 1))
done

for fixture in "$tests_dir"/fixtures/invalid/*.cbs; do
    name=${fixture##*/}
    if "$cbs" validate "$fixture" >"$temporary_dir/$name.out" 2>"$temporary_dir/$name.err"; then
        echo "FAIL: expected invalid: $name" >&2
        exit 1
    fi
    if ! grep -Eq '^.+:[0-9]+:[0-9]+: error\[CPDL-E[1239][0-9]{3}\]: (lex|parse|validation|internal): ' "$temporary_dir/$name.err"; then
        echo "FAIL: malformed diagnostic: $name" >&2
        cat "$temporary_dir/$name.err" >&2
        exit 1
    fi
    passed=$((passed + 1))
done

fixture="$tests_dir/fixtures/invalid/wrong-extension.cpdl"
if "$cbs" validate "$fixture" >"$temporary_dir/extension.out" 2>"$temporary_dir/extension.err"; then
    echo "FAIL: accepted .cpdl extension" >&2
    exit 1
fi
grep -q 'recipe must use the .cbs extension' "$temporary_dir/extension.err"
passed=$((passed + 1))

printf 'package "crlf" {\r\n    version "1"\r\n    release 1\r\n}\r\n' \
    >"$temporary_dir/crlf.cbs"
"$cbs" validate "$temporary_dir/crlf.cbs" >"$temporary_dir/crlf.out" \
    2>"$temporary_dir/crlf.err"
test ! -s "$temporary_dir/crlf.err"
passed=$((passed + 1))

printf '\357\273\277package "bom" { version "1" release 1 }\n' \
    >"$temporary_dir/bom.cbs"
if "$cbs" validate "$temporary_dir/bom.cbs" >"$temporary_dir/bom.out" \
    2>"$temporary_dir/bom.err"; then
    echo "FAIL: accepted UTF-8 byte-order mark" >&2
    exit 1
fi
grep -q 'error\[CPDL-E1001\]: lex:' "$temporary_dir/bom.err"
passed=$((passed + 1))

printf 'package "utf8" {\n    version "1\377"\n    release 1\n}\n' \
    >"$temporary_dir/invalid-utf8.cbs"
if "$cbs" validate "$temporary_dir/invalid-utf8.cbs" \
    >"$temporary_dir/utf8.out" 2>"$temporary_dir/utf8.err"; then
    echo "FAIL: accepted invalid UTF-8" >&2
    exit 1
fi
grep -q ':2:15: error\[CPDL-E1001\]: lex:' "$temporary_dir/utf8.err"
passed=$((passed + 1))

if "$cbs" validate >"$temporary_dir/usage.out" 2>"$temporary_dir/usage.err"; then
    echo "FAIL: invalid command line succeeded" >&2
    exit 1
fi
grep -q '^usage: cbs validate PACKAGE.cbs$' "$temporary_dir/usage.err"
passed=$((passed + 1))

"$cbs" --help >"$temporary_dir/help.out" 2>"$temporary_dir/help.err"
grep -q '^usage: cbs validate PACKAGE.cbs$' "$temporary_dir/help.out"
test ! -s "$temporary_dir/help.err"
passed=$((passed + 1))

printf 'parser and validation tests: PASS (%s cases)\n' "$passed"
