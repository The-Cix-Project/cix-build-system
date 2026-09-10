#!/bin/sh
set -eu

cbs=${1:?usage: cli-build-test.sh CBS}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temporary_dir=${TMPDIR:-/tmp}/cbs-cli-build-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/workspace" "$temporary_dir/cache"

artifact=$temporary_dir/standalone-smoke-x86_64-1.cixpkg
"$cbs" check "$tests_dir/fixtures/standalone-smoke.cbs" \
    >"$temporary_dir/check.out" 2>"$temporary_dir/check.err"
test ! -s "$temporary_dir/check.err"
"$cbs" explain "$tests_dir/fixtures/standalone-smoke.cbs" \
    >"$temporary_dir/explain.out" 2>"$temporary_dir/explain.err"
test ! -s "$temporary_dir/explain.err"
grep -q 'execution plan' "$temporary_dir/explain.out"
grep -q 'build operations=' "$temporary_dir/explain.out"
"$cbs" build "$tests_dir/fixtures/standalone-smoke.cbs" \
    --arch x86_64 --staged "$temporary_dir/workspace" \
    --output "$artifact" --cache "$temporary_dir/cache" \
    >"$temporary_dir/build.out" \
    2>"$temporary_dir/build.err"
test ! -s "$temporary_dir/build.err"
grep -q '^built ' "$temporary_dir/build.out"
"$cbs" verify "$artifact" >"$temporary_dir/verify.out"
grep -q 'verified CIXPKG (identity=standalone-smoke-1-1-x86_64)' \
    "$temporary_dir/verify.out"
test -f "$temporary_dir/workspace/dest/usr/bin/hello"
"$cbs" extract "$artifact" --into "$temporary_dir/extracted" >/dev/null
test "$(cat "$temporary_dir/extracted/usr/bin/hello")" = 'hello from CBS'
test "$(stat -c '%a' "$temporary_dir/extracted/usr/bin/hello")" = 755

mkdir -p -- "$temporary_dir/server/payload"
printf 'fetched source\n' >"$temporary_dir/server/payload/source.txt"
tar -cf "$temporary_dir/server/payload.tar" -C "$temporary_dir/server/payload" .
digest=$(sha256sum "$temporary_dir/server/payload.tar" | awk '{print $1}')
printf '%s\n' \
    'package "fetch-smoke" {' \
    '    version "1"' \
    '    release 1' \
    '    sources {' \
    '        main "payload" {' \
    "            url \"http://127.0.0.1:$((18000 + ($$ % 1000)))/payload.tar\"" \
    "            sha256 \"$digest\"" \
    '        }' \
    '    }' \
    '    build {' \
    '        require file "${src}/payload/source.txt" {' \
    '            exists' \
    '            contains "fetched source"' \
    '        }' \
    '        write "${dest}/fetched.txt" "yes"' \
    '    }' \
    '}' >"$temporary_dir/fetch-smoke.cbs"
port=$((18000 + ($$ % 1000)))
python3 -m http.server "$port" --bind 127.0.0.1 \
    --directory "$temporary_dir/server" >"$temporary_dir/http.log" 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true; rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
sleep 1
mkdir -p -- "$temporary_dir/fetch-workspace" "$temporary_dir/fetch-cache"
fetch_artifact=$temporary_dir/fetch-smoke.cixpkg
"$cbs" build "$temporary_dir/fetch-smoke.cbs" \
    --arch x86_64 --staged "$temporary_dir/fetch-workspace" \
    --output "$fetch_artifact" --cache "$temporary_dir/fetch-cache" \
    >"$temporary_dir/fetch.out" 2>"$temporary_dir/fetch.err"
test ! -s "$temporary_dir/fetch.err"
test -f "$temporary_dir/fetch-cache/$digest"
"$cbs" verify "$fetch_artifact" >/dev/null

printf 'CLI build tests: PASS (build, package, verify)\n'
