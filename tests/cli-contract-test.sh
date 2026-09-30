#!/bin/sh
set -eu

cbs=${1:?usage: cli-contract-test.sh CBS}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
version=$(sed -n '1p' VERSION)
temporary_dir=${TMPDIR:-/tmp}/cbs-cli-contract-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/workspace"

recipe=$temporary_dir/contract.cbs
artifact=$temporary_dir/contract.cixpkg
report=$temporary_dir/contract-report.json
bad_artifact=$temporary_dir/bad.cixpkg
cat >"$recipe" <<'EOF'
package "cli-contract" {
    version "1"
    release 1
    format "cixpkg"
    license "GPL-3.0-or-later"
    upstream "gitea-releases"
    requires {
        runtime { package "zstd" }
    }
    replaces {
        package "old-cli-contract"
    }
    metadata {
        "artifact_sha256" "deadbeef"
        "changelog" "contract metadata"
    }
    capability "CAP_ONE"
    capability "CAP_TWO"
    build {
        write "${dest}/hello" "hello\n" chmod 0755
        require file "${dest}/hello" { exists contains "hello" } for {
            "${dest}/hello"
            "${dest}/hello"
        }
        run "true" { each "first" "second" }
    }
}
EOF

test "$("$cbs" --version)" = "cbs $version"
"$cbs" --help >"$temporary_dir/help.out"
grep -q '^usage: cbs <command> \[options\]$' "$temporary_dir/help.out"
"$cbs" -h >"$temporary_dir/short-help.out"
cmp -s "$temporary_dir/help.out" "$temporary_dir/short-help.out"

test "$("$cbs" check "$recipe")" = "$recipe: valid CPDL 1.0"
test "$("$cbs" validate "$recipe")" = "$recipe: valid CPDL 1.0"
test "$("$cbs" validate "$recipe" --json)" = \
    "$recipe: valid CPDL 1.0"

"$cbs" explain "$recipe" >"$temporary_dir/explain.out"
grep -q "^$recipe: CPDL 1.0 execution plan (1 phases)$" \
    "$temporary_dir/explain.out"
grep -q '^1 build operations=5$' "$temporary_dir/explain.out"
"$cbs" explain "$recipe" --json >"$temporary_dir/explain.json"
grep -q '"name":"build"' "$temporary_dir/explain.json"
grep -q '"capabilities":\["CAP_ONE","CAP_TWO"\]' "$temporary_dir/explain.json"
grep -q '"metadata":{"artifact_sha256":"deadbeef","changelog":"contract metadata"}' \
    "$temporary_dir/explain.json"
grep -q '"license":"GPL-3.0-or-later"' "$temporary_dir/explain.json"
grep -q '"upstream":"gitea-releases"' "$temporary_dir/explain.json"
grep -q '"format":"cixpkg"' "$temporary_dir/explain.json"
grep -q '"runtime":{"package":\["zstd"\]}' "$temporary_dir/explain.json"
grep -q '"replaces":\["old-cli-contract"\]' "$temporary_dir/explain.json"

sed 's/format "cixpkg"/format "tar.gz"/' "$recipe" \
    >"$temporary_dir/tar-format.cbs"
if "$cbs" validate "$temporary_dir/tar-format.cbs" \
    >"$temporary_dir/tar-format.out" 2>"$temporary_dir/tar-format.err"; then
    echo 'unsupported tar.gz format unexpectedly validated' >&2
    exit 1
fi
grep -q 'CPDL-E3004' "$temporary_dir/tar-format.err"
grep -q 'artifact format must be cixpkg' "$temporary_dir/tar-format.err"

cat >"$temporary_dir/duplicate-metadata.cbs" <<'EOF'
package "duplicate-metadata" {
    version "1"
    release 1
    format "cixpkg"
    metadata { "key" "one" "key" "two" }
    build { mkdir "${dest}" }
}
EOF
set +e
"$cbs" validate "$temporary_dir/duplicate-metadata.cbs" \
    >"$temporary_dir/duplicate.out" 2>"$temporary_dir/duplicate.err"
status=$?
set -e
test "$status" -ne 0
grep -q 'duplicate metadata key' "$temporary_dir/duplicate.err"

cat >"$temporary_dir/non-string-metadata.cbs" <<'EOF'
package "non-string-metadata" {
    version "1"
    release 1
    format "cixpkg"
    metadata { "key" 1 }
    build { mkdir "${dest}" }
}
EOF
set +e
"$cbs" validate "$temporary_dir/non-string-metadata.cbs" \
    >"$temporary_dir/non-string.out" 2>"$temporary_dir/non-string.err"
status=$?
set -e
test "$status" -ne 0
grep -q 'metadata value' "$temporary_dir/non-string.err"

"$cbs" inspect "$recipe" >"$temporary_dir/inspect.out"
grep -Eq '^recipe-digest [0-9a-f]{64}$' "$temporary_dir/inspect.out"
test "$(wc -l <"$temporary_dir/inspect.out")" -eq 2
grep -q '^license GPL-3.0-or-later$' "$temporary_dir/inspect.out"
fingerprint_one=$("$cbs" fingerprint "$recipe" --arch x86_64)
fingerprint_two=$("$cbs" fingerprint "$recipe" --arch x86_64)
test "$fingerprint_one" = "$fingerprint_two"
printf '%s\n' "$fingerprint_one" | grep -Eq \
    '^fingerprint [0-9a-f]{64}$'
printf one >"$temporary_dir/input-one"
printf two >"$temporary_dir/input-two"
ordered_one=$("$cbs" fingerprint "$recipe" --arch x86_64 \
    --input first="$temporary_dir/input-one" \
    --input second="$temporary_dir/input-two")
ordered_two=$("$cbs" fingerprint "$recipe" --arch x86_64 \
    --input second="$temporary_dir/input-two" \
    --input first="$temporary_dir/input-one")
test "$ordered_one" = "$ordered_two"
printf changed >"$temporary_dir/input-one"
changed_input=$("$cbs" fingerprint "$recipe" --arch x86_64 \
    --input first="$temporary_dir/input-one" \
    --input second="$temporary_dir/input-two")
test "$ordered_one" != "$changed_input"
tool_identity='gcc@14.2=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
identity_fingerprint=$($cbs fingerprint "$recipe" --arch x86_64 \
    --tool-identity "$tool_identity")
printf '%s\n' "$identity_fingerprint" | grep -Eq \
    '^fingerprint [0-9a-f]{64}$'
identity_workspace="$temporary_dir/identity-workspace"
identity_artifact="$temporary_dir/identity.cixpkg"
identity_report="$temporary_dir/identity-report.json"
mkdir "$identity_workspace"
"$cbs" build "$recipe" --arch x86_64 --staged "$identity_workspace" \
    --output "$identity_artifact" --report "$identity_report" \
    --tool-identity "$tool_identity" >"$temporary_dir/identity-build.out"
identity_digest=$(sed -n 's/.*"fingerprint":"\([^"]*\)".*/\1/p' \
    "$identity_report")
test "$identity_fingerprint" = "fingerprint $identity_digest"
"$cbs" list "$identity_artifact" --json >"$temporary_dir/identity-list.json"
identity_artifact_digest=$(sed -n \
    's/.*"key":"build_fingerprint","value":"\([^"]*\)".*/\1/p' \
    "$temporary_dir/identity-list.json")
test "$identity_fingerprint" = "fingerprint $identity_artifact_digest"
tool_recipe="$tests_dir/fixtures/tool-policy.cbs"
tool_path="$temporary_dir/tools"
mkdir "$tool_path"
printf '%s\n' '#!/bin/sh' 'printf "%s\\n" "$@" >> args' >"$tool_path/tcc"
chmod 755 "$tool_path/tcc"
tool_command_path="$tool_path:/usr/bin:/bin"
tool_fingerprint=$($cbs fingerprint "$tool_recipe" --arch x86_64 \
    --command-path "$tool_command_path")
tool_workspace="$temporary_dir/tool-workspace"
tool_artifact="$temporary_dir/tool-policy.cixpkg"
tool_report="$temporary_dir/tool-policy-report.json"
mkdir "$tool_workspace"
"$cbs" build "$tool_recipe" --arch x86_64 --staged "$tool_workspace" \
    --output "$tool_artifact" --report "$tool_report" \
    --command-path "$tool_command_path" \
    >"$temporary_dir/tool-build.out"
tool_report_digest=$(sed -n 's/.*"fingerprint":"\([^"]*\)".*/\1/p' \
    "$tool_report")
test "$tool_fingerprint" = "fingerprint $tool_report_digest"
"$cbs" list "$tool_artifact" --json >"$temporary_dir/tool-list.json"
tool_artifact_digest=$(sed -n \
    's/.*"key":"build_fingerprint","value":"\([^"]*\)".*/\1/p' \
    "$temporary_dir/tool-list.json")
test "$tool_fingerprint" = "fingerprint $tool_artifact_digest"
"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/workspace" \
    --output "$artifact" --report "$report" >"$temporary_dir/build.out"
test "$(cat "$temporary_dir/build.out")" = "built $artifact"
grep -q '"schema":"cbs.build-report/v1"' "$report"
grep -q '"status":0' "$report"
grep -q '"artifact_digest":"[0-9a-f]\{64\}"' "$report"
if grep -q '"fingerprint":"unavailable"' "$report"; then
    :
else
    grep -q '"fingerprint":"[0-9a-f]\{64\}"' "$report"
fi
"$cbs" list "$artifact" --json >"$temporary_dir/list.json"
grep -q '"schema":"cbs.cixpkg-list/v2"' "$temporary_dir/list.json"
grep -q '"path":"hello"' "$temporary_dir/list.json"
"$cbs" list "$artifact" --json --path-prefix hello --type file \
    >"$temporary_dir/list-filtered.json"
grep -q '"path":"hello"' "$temporary_dir/list-filtered.json"
test "$(grep -o '"path"' "$temporary_dir/list-filtered.json" | wc -l)" -eq 1
"$cbs" diff "$artifact" "$artifact" >"$temporary_dir/self-diff.out"
test ! -s "$temporary_dir/self-diff.out"
mkdir "$temporary_dir/different-stage"
cp "$temporary_dir/workspace/dest/hello" \
    "$temporary_dir/different-stage/hello"
printf 'changed\n' >"$temporary_dir/different-stage/hello"
different_artifact="$temporary_dir/different.cixpkg"
"$cbs" package "$temporary_dir/different-stage" --name cli-contract \
    --version 1 --release 1 --arch x86_64 --output "$different_artifact" \
    >/dev/null
set +e
"$cbs" diff "$artifact" "$different_artifact" \
    >"$temporary_dir/diff.out"
status=$?
set -e
test "$status" -eq 1
grep -q '^~ hello \[' "$temporary_dir/diff.out"
set +e
"$cbs" diff "$artifact" "$different_artifact" --path-prefix hello \
    --type file >"$temporary_dir/diff-filtered.out"
filtered_status=$?
set -e
test "$filtered_status" -eq 1
grep -q '^~ hello \[' "$temporary_dir/diff-filtered.out"
"$cbs" inspect "$recipe" "$artifact" >"$temporary_dir/inspect-artifact.out"
grep -Eq '^artifact-digest [0-9a-f]{64}$' "$temporary_dir/inspect-artifact.out"
grep -q '^license GPL-3.0-or-later$' "$temporary_dir/inspect-artifact.out"
grep -q '^artifact-license GPL-3.0-or-later$' \
    "$temporary_dir/inspect-artifact.out"
test "$(wc -l <"$temporary_dir/inspect-artifact.out")" -eq 4

test "$("$cbs" verify "$artifact")" = \
    "$artifact: verified CIXPKG (identity=cli-contract-1-1-x86_64)"
test "$("$cbs" extract "$artifact" --into "$temporary_dir/extracted")" = \
    "extracted $temporary_dir/extracted"
test "$(cat "$temporary_dir/extracted/hello")" = hello
test "$(stat -c '%a' "$temporary_dir/extracted/hello")" = 755

staged_artifact="$temporary_dir/staged-tree.cixpkg"
staged_extract="$temporary_dir/staged-tree-extracted"
test "$($cbs package "$temporary_dir/workspace/dest" \
    --name hostbuild --version 1 --release 1 --arch x86_64 \
    --license MIT --output "$staged_artifact")" = \
    "packaged $staged_artifact"
test "$($cbs verify "$staged_artifact")" = \
    "$staged_artifact: verified CIXPKG (identity=hostbuild-1-1-x86_64)"
test "$($cbs extract "$staged_artifact" --into "$staged_extract")" = \
    "extracted $staged_extract"
test "$(cat "$staged_extract/hello")" = hello

# A dynamic or relative run target is valid, but its tool identity cannot yet
# be measured. Packaging must record that fact instead of failing the build.
cat >"$temporary_dir/unmeasurable.cbs" <<'EOF'
package "unmeasurable" {
    version "1"
    release 1
    format "cixpkg"
    build {
        write "${build}/configure" "#!/bin/sh\nexit 0\n" chmod 0755
        run "./configure" {}
        run "${build}/configure" {}
        write "${dest}/done" "yes\n"
    }
}
EOF
unmeasurable_artifact="$temporary_dir/unmeasurable.cixpkg"
unmeasurable_report="$temporary_dir/unmeasurable-report.json"
mkdir "$temporary_dir/unmeasurable-workspace"
"$cbs" build "$temporary_dir/unmeasurable.cbs" --arch x86_64 \
    --staged "$temporary_dir/unmeasurable-workspace" \
    --output "$unmeasurable_artifact" --report "$unmeasurable_report" \
    >"$temporary_dir/unmeasurable.out"
test "$(cat "$temporary_dir/unmeasurable.out")" = \
    "built $unmeasurable_artifact"
grep -q '"fingerprint":"unavailable"' "$unmeasurable_report"
"$cbs" list "$unmeasurable_artifact" --json >"$temporary_dir/unmeasurable-list.json"
grep -q 'build_fingerprint.*unavailable' "$temporary_dir/unmeasurable-list.json"
grep -q 'build_fingerprint_reason.*dynamic-run:./configure' \
    "$temporary_dir/unmeasurable-list.json"

empty_stage="$temporary_dir/empty-stage"
empty_artifact="$temporary_dir/empty-stage.cixpkg"
mkdir "$empty_stage"
test "$($cbs package "$empty_stage" --name empty --version 1 --release 1 \
    --arch x86_64 --output "$empty_artifact")" = \
    "packaged $empty_artifact"
"$cbs" verify "$empty_artifact" >/dev/null

set +e
"$cbs" extract "$artifact" --into "$temporary_dir/extracted" \
    >"$temporary_dir/existing-destination.out" \
    2>"$temporary_dir/existing-destination.err"
status=$?
set -e
test "$status" -eq 4
grep -q 'error\[CIXPKG-E4003\].*destination already exists' \
    "$temporary_dir/existing-destination.err"

set +e
"$cbs" extract "$artifact" --into "$temporary_dir/missing/out" \
    >"$temporary_dir/missing-parent.out" \
    2>"$temporary_dir/missing-parent.err"
status=$?
set -e
test "$status" -eq 4
grep -q 'error\[CIXPKG-E4004\].*parent does not exist' \
    "$temporary_dir/missing-parent.err"

cp "$artifact" "$bad_artifact"
printf 'x' | dd of="$bad_artifact" bs=1 seek=0 conv=notrunc 2>/dev/null
set +e
"$cbs" extract "$bad_artifact" --into "$temporary_dir/bad-extracted" \
    >"$temporary_dir/bad-extract.out" 2>"$temporary_dir/bad-extract.err"
status=$?
set -e
test "$status" -eq 4
grep -q 'error\[CIXPKG-E4001\].*artifact verification failed' \
    "$temporary_dir/bad-extract.err"

set +e
"$cbs" verify "$bad_artifact" --diagnostics=jsonl \
    >"$temporary_dir/bad-json.out" 2>"$temporary_dir/bad-json.err"
status=$?
set -e
test "$status" -eq 4
grep -q '"schema":"cbs.diagnostic/v2"' "$temporary_dir/bad-json.err"
grep -q '"verb":"verify"' "$temporary_dir/bad-json.err"
grep -q '"code":"CIXPKG-E4001"' "$temporary_dir/bad-json.err"

cat >"$temporary_dir/empty-list.cbs" <<'EOF'
package "empty-list" {
    version "1"
    release 1
    format "cixpkg"
    build { run "true" { each } }
}
EOF
set +e
"$cbs" validate "$temporary_dir/empty-list.cbs" \
    >"$temporary_dir/empty-list.out" 2>"$temporary_dir/empty-list.err"
status=$?
set -e
test "$status" -ne 0
grep -q 'list declaration must contain at least one value' \
    "$temporary_dir/empty-list.err"

set +e
"$cbs" validate "$temporary_dir/empty-list.cbs" --diagnostics=jsonl \
    >"$temporary_dir/empty-list-json.out" \
    2>"$temporary_dir/empty-list-json.err"
status=$?
set -e
test "$status" -ne 0
grep -q '"schema":"cbs.diagnostic/v2"' \
    "$temporary_dir/empty-list-json.err"
grep -q '"verb":"validate"' "$temporary_dir/empty-list-json.err"
grep -q '"code":"CPDL-E3004"' "$temporary_dir/empty-list-json.err"

cat >"$temporary_dir/failing-list.cbs" <<'EOF'
package "failing-list" {
    version "1"
    release 1
    format "cixpkg"
    build {
        mkdir "${dest}"
        require file "${dest}/missing" { exists } for {
            "${dest}/missing"
        }
    }
}
EOF
set +e
mkdir -p "$temporary_dir/failing-stage"
"$cbs" build "$temporary_dir/failing-list.cbs" --arch x86_64 \
    --staged "$temporary_dir/failing-stage" \
    --output "$temporary_dir/failing.cixpkg" \
    --report "$temporary_dir/failing-report.json" \
    >"$temporary_dir/failing-list.out" 2>"$temporary_dir/failing-list.err"
status=$?
set -e
test "$status" -ne 0
grep -q '/missing' "$temporary_dir/failing-list.err"
grep -q '"schema":"cbs.build-report/v1"' \
    "$temporary_dir/failing-report.json"
grep -q '"status":1' "$temporary_dir/failing-report.json"

set +e
"$cbs" build "$temporary_dir/failing-list.cbs" --arch x86_64 \
    --staged "$temporary_dir/failing-stage-json" \
    --output "$temporary_dir/failing-json.cixpkg" --diagnostics=jsonl \
    >"$temporary_dir/failing-json.out" 2>"$temporary_dir/failing-json.err"
status=$?
set -e
test "$status" -ne 0
grep -q '"schema":"cbs.diagnostic/v2"' \
    "$temporary_dir/failing-json.err"
grep -q '"verb":"build"' "$temporary_dir/failing-json.err"

set +e
"$cbs" package "$temporary_dir/missing-stage" --name bad --version 1 \
    --release 1 --arch x86_64 --output "$temporary_dir/bad-package.cixpkg" \
    --diagnostics=jsonl >"$temporary_dir/package-json.out" \
    2>"$temporary_dir/package-json.err"
status=$?
set -e
test "$status" -eq 3
grep -q '"verb":"package"' "$temporary_dir/package-json.err"
grep -q '"code":"CBS-E1018"' "$temporary_dir/package-json.err"

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
