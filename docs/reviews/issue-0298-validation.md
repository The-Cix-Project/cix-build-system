# Issue #298 validation: build event sequence numbers repeat within a build

## Cause

`cbs_emit_build_event()` advanced the counter held in whichever context it was
handed. A phase executes in a copy — `CbsExecutionContext local = *context;` —
and so does a `cd` block and a `check` case. The copy's counter advanced for
every command event and was never carried back, so the outer context emitted
that phase's `phase-end` from its own stale value and the next phase reissued
numbers already used.

Measured before the change, on the three-phase four-command fixture added here:
**17 events, 9 distinct sequence numbers**, with 3, 4, 5, 6, 7 and 8 each issued
twice. `prepare`'s `phase-end` carried 3 while the commands inside that phase
carried 3, 4, 5 and 6. The integration contract's claim that sequence numbers
"are monotonically increasing within a build" was false.

## Decision

One counter belongs to the build. `CbsExecutionContext` gains
`unsigned long long *sequence_counter`, pointing at the originating context's
`event_sequence`; the pipeline sets it once, and every copy inherits the
pointer rather than a value.

Carrying the counter back from each copy was rejected. There are three copy
sites today, and a write-back missed at any one of them reintroduces the defect
silently — the same failure mode as a field missed in `clone_node()`. A shared
pointer is correct by construction: a new copy site cannot get it wrong.

This changes the public struct, so `CBS_ABI_VERSION` goes 3 → 4 and an embedder
must rebuild against the matching header. `cbs_execution_context_size()` moves
from 472 to 480, which is exactly what the documented version guard exists to
catch. Nothing pinned the ABI version to a literal, so no test or capability
assertion needed changing.

A NULL `sequence_counter` falls back to the local field, so a caller that
zeroes the struct and drives execution itself keeps its current behaviour
rather than dereferencing NULL.

## The second effect, which was worse than reported

The ticket noted that `exec.c` names each command log from the event counter and
predicted a collision across phases, marked "not observed". It is observable,
and in two different ways.

With a sink attached, the predicted collision happens: pre-fix, the four
commands reported `command-3.log`, `command-5.log`, `command-5.log` and
`command-7.log` — `prepare`'s second command and `build`'s first both claimed
the same file, so one overwrote the other.

Without a sink it was total. `cbs_emit_build_event()` returned early when
`event_sink` was NULL, before touching the counter, so the counter never moved
and **every** command's log was named `command-1.log`: four commands, one file,
three overwritten. `CBS_LOG_DIR` is documented as an opt-in a caller may set on
its own, so that was the common case rather than an edge one.

The counter therefore now advances *before* the sink check. A build numbers its
events — and the logs named from them — identically whether or not anyone is
listening, which is a property worth having on its own: a log's name no longer
depends on who was watching.

## Acceptance criteria

1. **Monotonic and distinct** — `tests/observability-test.sh` asserts the event
   count equals the distinct-sequence count and that the numbers sort in
   emission order.
2. **The shape that regressed** — asserts the first `phase-end` does not precede
   any command in its own phase. That is the specific inversion a consumer saw.
3. **One log per command** — four commands, four logs.
4. **Log names do not depend on the sink** — the same build run with and
   without `--events-file` produces identical log listings, compared with `cmp`.

`tests/fixtures/observability-sequence.cbs` is new because
`observability-success.cbs` has one phase and one command and cannot express
the defect, which is why it went unnoticed.

Verified by stashing only `src/` and running the new assertions against the
pre-fix engine: they fail with "17 events but only 9 distinct sequence
numbers".

## Docs

Integration contract — the guarantee, that it was broken until v0.1.117, the
ABI bump, and the sink-independent numbering. Library guide — `CBS_ABI_VERSION`
is 4 and why. `docs/cpdl-test-coverage.md`.
