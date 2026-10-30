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
# Issue #297: the command's own result, not only whether it met expectation.
# `false` exits 1 and was not signalled.
grep -F '"exit_status":1,"signal":0' "$root/failure" >/dev/null
grep '"log_path":"[^"]*command-' "$root/failure" >/dev/null

if "$cbs" build tests/fixtures/observability-timeout.cbs \
    --arch x86_64 --staged "$stage" --events=jsonl \
    >"$root/timeout.stdout" 2>"$root/timeout"; then
    echo 'observability tests: timeout unexpectedly succeeded' >&2
    exit 1
fi
grep 'process timed out' "$root/timeout" >/dev/null
# A killed command reports the signal and no exit code, so a consumer can tell
# it from one that exited non-zero -- both are status 1.
grep -E '"exit_status":-1,"signal":(15|9)' "$root/timeout" >/dev/null
# The human reporter names the cause rather than printing a bare FAILED.
"$cbs" build tests/fixtures/observability-failure.cbs \
    --arch x86_64 --staged "$stage" --events=human \
    >"$root/failure.human.out" 2>"$root/failure.human" || true
grep -F 'FAILED (exit 1)' "$root/failure.human" >/dev/null
"$cbs" build tests/fixtures/observability-timeout.cbs \
    --arch x86_64 --staged "$stage" --events=human \
    >"$root/timeout.human.out" 2>"$root/timeout.human" || true
grep -E 'FAILED \(signal (15|9)\)' "$root/timeout.human" >/dev/null
# A command that met its expectation carries its exit code and no signal.
grep -F '"type":"command-end"' "$root/events" >/dev/null
grep -F '"status":0,"exit_status":0,"signal":0' "$root/events" >/dev/null

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

# Establish the closed descriptor rather than assume one is free.  An unused
# number is only closed if nothing up the process tree left it open, and CBS
# builds CBS: `make test` runs the freshly built ./cbs inside the outer cbs's
# command tree, so the fresh binary inherits whatever that parent held.  This
# assertion failed on Cix for exactly that reason, not because the refusal was
# wrong.
exec 9>&-

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

# Issue #298: one counter for the build. A phase or `cd` block runs in a copy
# of the execution context, so a counter held in the copy numbered events
# nobody else saw: phase-end carried a number lower than the commands inside
# that phase, and the next phase reused numbers already issued.
mkdir "$root/seqstage" "$root/seqlogs" "$root/seqlogs-nosink"
CBS_LOG_DIR="$root/seqlogs" "$cbs" build \
    tests/fixtures/observability-sequence.cbs \
    --arch x86_64 --staged "$root/seqstage" --output "$root/seq.cixpkg" \
    --events-file "$root/seq.events" >"$root/seq.out" 2>"$root/seq.err"

# Strictly increasing, with no value issued twice.
sed -n 's/.*"sequence":\([0-9]*\).*/\1/p' "$root/seq.events" >"$root/seq.nums"
total=$(wc -l <"$root/seq.nums")
distinct=$(sort -u -n "$root/seq.nums" | wc -l)
if [ "$total" -ne "$distinct" ]; then
    echo "observability tests: $total events but only $distinct distinct" \
        "sequence numbers" >&2
    exit 1
fi
if ! sort -n -c "$root/seq.nums" 2>/dev/null; then
    echo 'observability tests: event sequence numbers are not increasing' >&2
    exit 1
fi
# The shape that regressed: the phase's own phase-end must come after every
# command inside it, not before.
first_phase_end=$(sed -n 's/.*"type":"phase-end".*"sequence":\([0-9]*\).*/\1/p' \
    "$root/seq.events" | head -1)
last_prepare_cmd=$(grep '"phase":"prepare"' "$root/seq.events" |
    sed -n 's/.*"sequence":\([0-9]*\).*/\1/p' | sort -n | tail -1)
test -n "$first_phase_end"
test -n "$last_prepare_cmd"
if [ "$first_phase_end" -lt "$last_prepare_cmd" ]; then
    echo "observability tests: phase-end ($first_phase_end) precedes a command" \
        "in its own phase ($last_prepare_cmd)" >&2
    exit 1
fi

# One log per command, and the same names whether or not events are consumed:
# the counter advances before the sink check, so a log name does not depend on
# anyone listening. Four commands run in that fixture.
test "$(ls "$root/seqlogs" | wc -l)" -eq 4
CBS_LOG_DIR="$root/seqlogs-nosink" "$cbs" build \
    tests/fixtures/observability-sequence.cbs \
    --arch x86_64 --staged "$root/seqstage" --output "$root/seq2.cixpkg" \
    >"$root/seq2.out" 2>"$root/seq2.err"
test "$(ls "$root/seqlogs-nosink" | wc -l)" -eq 4
ls "$root/seqlogs" | sort >"$root/seqlogs.list"
ls "$root/seqlogs-nosink" | sort >"$root/seqlogs-nosink.list"
if ! cmp -s "$root/seqlogs.list" "$root/seqlogs-nosink.list"; then
    echo 'observability tests: command log names depend on the event sink' >&2
    exit 1
fi

echo 'observability tests: PASS (events, reporters, event destinations, sequence numbering, command results, redaction, logs, metrics, failure, timeout)'
