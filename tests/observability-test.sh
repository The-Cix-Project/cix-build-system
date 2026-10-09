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

# Issue #282: the event stream can be pointed away from stderr, so a caller
# can read structured events while the human log keeps stderr to itself.

# The cixd case: human reporter on stderr and JSONL on a descriptor of the
# caller's own, from one build.
CBS_LOG_DIR="$logs" "$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --output "$root/fd.cixpkg" \
    --events human --events-fd 3 \
    3>"$root/fd.events" >"$root/fd.stdout" 2>"$root/fd.human"
grep -F 'run true (log:' "$root/fd.human" >/dev/null
grep -F '"type":"build-begin"' "$root/fd.events" >/dev/null
grep -F '"type":"command-end"' "$root/fd.events" >/dev/null
# Each stream carries only its own form.
if grep -F '{"version":' "$root/fd.human" >/dev/null; then
    echo 'observability tests: JSONL leaked into the human log' >&2
    exit 1
fi
if grep -F 'run true (log:' "$root/fd.events" >/dev/null; then
    echo 'observability tests: human prose leaked into the event stream' >&2
    exit 1
fi

# --events-file writes the same stream to a path, and an explicit destination
# with --events jsonl moves it off stderr rather than duplicating it.
"$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --output "$root/file.cixpkg" \
    --events jsonl --events-file "$root/file.events" \
    >"$root/file.stdout" 2>"$root/file.stderr"
grep -F '"type":"build-begin"' "$root/file.events" >/dev/null
test ! -s "$root/file.stderr"

# A destination with no --events still writes the structured stream.
"$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --output "$root/bare.cixpkg" \
    --events-file "$root/bare.events" >"$root/bare.stdout" 2>"$root/bare.stderr"
grep -F '"type":"build-end"' "$root/bare.events" >/dev/null

# --report still aggregates when the stream is tee'd to a destination.
"$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --output "$root/both.cixpkg" \
    --events human --events-file "$root/both.events" \
    --report "$root/both.report.json" \
    >"$root/both.stdout" 2>"$root/both.human"
grep -F '"type":"build-end"' "$root/both.events" >/dev/null
grep -F '"schema":"cbs.build-report/v1"' "$root/both.report.json" >/dev/null

# Refusals are usage errors, not a build that silently reports nothing.
for bad in '--events-fd abc' '--events-fd -1' '--events-fd 9'; do
    # shellcheck disable=SC2086
    if "$cbs" build tests/fixtures/observability-success.cbs \
        --arch x86_64 --staged "$stage" --events jsonl $bad \
        >"$root/bad.stdout" 2>"$root/bad.stderr"; then
        echo "observability tests: \`$bad\` was accepted" >&2
        exit 1
    fi
    grep -F 'CBS-E1022' "$root/bad.stderr" >/dev/null
done
# A read-only descriptor is refused rather than failing deep in the build.
if "$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --events jsonl --events-fd 0 \
    </dev/null >"$root/ro.stdout" 2>"$root/ro.stderr"; then
    echo 'observability tests: a read-only event descriptor was accepted' >&2
    exit 1
fi
grep -F 'is not open for writing' "$root/ro.stderr" >/dev/null
if "$cbs" build tests/fixtures/observability-success.cbs \
    --arch x86_64 --staged "$stage" --events jsonl \
    --events-fd 3 --events-file "$root/x.events" 3>/dev/null \
    >"$root/both.bad.stdout" 2>"$root/both.bad.stderr"; then
    echo 'observability tests: two event destinations were accepted' >&2
    exit 1
fi
grep -F 'cannot be combined' "$root/both.bad.stderr" >/dev/null

echo 'observability tests: PASS (events, reporters, event destinations, redaction, logs, metrics, failure, timeout)'
