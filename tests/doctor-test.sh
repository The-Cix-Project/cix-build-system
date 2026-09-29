#!/bin/sh
set -eu

cbs=${1:?usage: doctor-test.sh CBS}
temporary_dir=${TMPDIR:-/tmp}/cbs-doctor-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/workspace" "$temporary_dir/tools"

"$cbs" doctor tests/fixtures/standalone-smoke.cbs \
    --arch x86_64 --staged "$temporary_dir/workspace" \
    >"$temporary_dir/pass.out"
grep -q 'doctor: PASS recipe:' "$temporary_dir/pass.out"
grep -q 'doctor: PASS summary:' "$temporary_dir/pass.out"

cat >"$temporary_dir/missing.cbs" <<'EOF'
package "doctor-missing" {
    version "1"
    release 1
    format "cixpkg"
    build { run "definitely-not-a-cbs-command" { } }
}
EOF
set +e
"$cbs" doctor "$temporary_dir/missing.cbs" --diagnostics=jsonl \
    >"$temporary_dir/fail.out" 2>"$temporary_dir/fail.err"
status=$?
set -e
test "$status" -eq 3
grep -q '"schema":"cbs.diagnostic/v2"' "$temporary_dir/fail.err"
grep -q '"verb":"doctor"' "$temporary_dir/fail.err"
grep -q 'executable is absent' "$temporary_dir/fail.err"

before=$(find "$temporary_dir/workspace" -mindepth 1 -maxdepth 1 -printf '%f\n' | sort)
"$cbs" doctor tests/fixtures/standalone-smoke.cbs \
    --staged "$temporary_dir/workspace" >/dev/null
after=$(find "$temporary_dir/workspace" -mindepth 1 -maxdepth 1 -printf '%f\n' | sort)
test "$before" = "$after"

printf '%s\n' 'doctor tests: PASS (preflight, diagnostics, and no mutation)'
