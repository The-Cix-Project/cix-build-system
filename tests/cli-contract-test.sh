#!/bin/sh
set -eu

cbs=${1:?usage: cli-contract-test.sh CBS}
version=$(sed -n '1p' VERSION)
temporary_dir=${TMPDIR:-/tmp}/cbs-cli-contract-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/workspace"

recipe=$temporary_dir/contract.cbs
artifact=$temporary_dir/contract.cixpkg
bad_artifact=$temporary_dir/bad.cixpkg
cat >"$recipe" <<'EOF'
package "cli-contract" {
    version "1"
    release 1
    format "cixpkg"
    build {
        write "${dest}/hello" "hello\n" chmod 0755
        require file "${dest}/hello" { exists contains "hello" }
    }
}
EOF

test "$("$cbs" --version)" = "cbs $version"
"$cbs" --help >"$temporary_dir/help.out"
grep -q '^usage: cbs <command> \[options\]$' "$temporary_dir/help.out"
"$cbs" -h >"$temporary_dir/short-help.out"
cmp -s "$temporary_dir/help.out" "$temporary_dir/short-help.out"

test "$("$cbs" check "$recipe")" = "$recipe: valid CPDL 0.1"
test "$("$cbs" validate "$recipe")" = "$recipe: valid CPDL 0.1"
test "$("$cbs" validate "$recipe" --json)" = \
    "$recipe: valid CPDL 0.1"

"$cbs" explain "$recipe" >"$temporary_dir/explain.out"
grep -q "^$recipe: CPDL 0.1 execution plan (1 phases)$" \
    "$temporary_dir/explain.out"
grep -q '^1 build operations=2$' "$temporary_dir/explain.out"
"$cbs" explain "$recipe" --json >"$temporary_dir/explain.json"
grep -q '"name":"build"' "$temporary_dir/explain.json"

"$cbs" inspect "$recipe" >"$temporary_dir/inspect.out"
grep -Eq '^recipe-digest [0-9a-f]{64}$' "$temporary_dir/inspect.out"
test "$(wc -l <"$temporary_dir/inspect.out")" -eq 1

"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/workspace" \
    --output "$artifact" >"$temporary_dir/build.out"
test "$(cat "$temporary_dir/build.out")" = "built $artifact"
"$cbs" inspect "$recipe" "$artifact" >"$temporary_dir/inspect-artifact.out"
grep -Eq '^artifact-digest [0-9a-f]{64}$' "$temporary_dir/inspect-artifact.out"
test "$(wc -l <"$temporary_dir/inspect-artifact.out")" -eq 2

test "$("$cbs" verify "$artifact")" = \
    "$artifact: verified CIXPKG (identity=cli-contract-1-1-x86_64)"
test "$("$cbs" extract "$artifact" --into "$temporary_dir/extracted")" = \
    "extracted $temporary_dir/extracted"
test "$(cat "$temporary_dir/extracted/hello")" = hello
test "$(stat -c '%a' "$temporary_dir/extracted/hello")" = 755

cp "$artifact" "$bad_artifact"
printf 'x' | dd of="$bad_artifact" bs=1 seek=0 conv=notrunc 2>/dev/null
set +e
"$cbs" verify "$bad_artifact" >"$temporary_dir/bad.out" \
    2>"$temporary_dir/bad.err"
status=$?
set -e
test "$status" -eq 4
grep -q 'error\[CIXPKG-E4001\]' "$temporary_dir/bad.err"

set +e
"$cbs" build "$recipe" --arch x86_64 \
    >"$temporary_dir/missing-staged.out" 2>"$temporary_dir/missing-staged.err"
status=$?
set -e
test "$status" -eq 2
grep -q -- '--arch and --staged are required' "$temporary_dir/missing-staged.err"

set +e
"$cbs" verify "$temporary_dir/missing.cixpkg" \
    >"$temporary_dir/missing.out" 2>"$temporary_dir/missing.err"
status=$?
set -e
test "$status" -eq 4

printf '%s\n' 'CLI contract tests: PASS (commands, output, and exit statuses)'
