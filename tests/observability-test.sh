#!/bin/sh
set -eu

cbs=${1:?usage: observability-test.sh CBS}
root=$(mktemp -d)
trap 'chmod -R u+w -- "$root" 2>/dev/null || true; rm -rf -- "$root"' EXIT HUP INT TERM
stage="$root/stage"
logs="$root/logs"
mkdir "$stage" "$logs"

CBS_LOG_DIR="$logs" "$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --output "$root/out.cixpkg" --events=jsonl \
    >"$root/stdout" 2>"$root/events"

grep -F '"type":"build-begin"' "$root/events" >/dev/null
grep -F '"type":"command-end"' "$root/events" >/dev/null
grep -F '"environment_names":"PATH,SOURCE_DATE_EPOCH,CBS_SECRET_TOKEN"' "$root/events" >/dev/null
grep -E '"tree_files":[1-9]' "$root/events" >/dev/null
grep -E '"tree_bytes":[1-9]' "$root/events" >/dev/null
grep -E '"timestamp_ms":[1-9]' "$root/events" >/dev/null
if grep -F 'must-not-appear' "$root/events" >/dev/null; then
    echo 'observability tests: secret leaked' >&2
    exit 1
fi
test -f "$logs/command-3.log"

CBS_LOG_DIR="$logs" "$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --output "$root/human.cixpkg" --events=human \
    >"$root/human.stdout" 2>"$root/human"
grep -F 'run true (log:' "$root/human" >/dev/null

if CBS_LOG_DIR="$logs" "$cbs" build tests/fixtures/observability-failure.cbs \
    --arch x86_64 --staged "$stage" --events=jsonl \
    >"$root/failure.stdout" 2>"$root/failure"; then
    echo 'observability tests: failed command unexpectedly succeeded' >&2
    exit 1
fi
grep -F '"type":"command-end"' "$root/failure" >/dev/null
grep -F '"message":"command failed"' "$root/failure" >/dev/null
grep '"log_path":"[^"]*command-' "$root/failure" >/dev/null

if "$cbs" build tests/fixtures/observability-timeout.cbs \
    --arch x86_64 --staged "$stage" --events=jsonl \
    >"$root/timeout.stdout" 2>"$root/timeout"; then
    echo 'observability tests: timeout unexpectedly succeeded' >&2
    exit 1
fi
grep 'process timed out' "$root/timeout" >/dev/null

echo 'observability tests: PASS (events, reporters, redaction, logs, metrics, failure, timeout)'
