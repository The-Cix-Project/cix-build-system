# Issue #297 validation: `command-end` carried only whether a command met its expectation

## The gap

`command-end`'s `status` is 0 or 1 — whether the command exited with the status
its recipe expected. Nothing carried the command's own result, so a command
killed by a signal and one that exited 2 produced identical events.

Measured before the change: a `run "false" { expect exit 0 }` and a
`run "sleep" { "5" timeout 1s }` both emitted
`status=1, message="command failed"`, and `status` was the only result field on
the event.

## Decision

Two fields on `CbsBuildEvent`, so `status` keeps its meaning:

- `exit_status` — the command's own exit code, or **-1** when it did not exit;
- `signal` — the terminating signal, or **0** when it was not killed.

| case | `status` | `exit_status` | `signal` |
| --- | --- | --- | --- |
| `true`, expected 0 | 0 | 0 | 0 |
| `false`, expected 0 | 1 | 1 | 0 |
| `sleep 5`, `timeout 1s` | 1 | -1 | 15 |

The human reporter names the cause rather than printing a bare `FAILED`:
`false FAILED (exit 1)` and `sleep FAILED (signal 15)`.

`cbs_emit_build_event()` is declared only in the repository-private `src/cbs.h`,
so its signature could have changed directly. It did not: the single emit path
is now a static `emit_event()` taking the pair, with `cbs_emit_build_event()`
passing zeroes and a new `cbs_emit_command_end()` passing real values. Every
existing call site is untouched, which keeps the diff to the one event that has
a command result rather than threading two arguments through thirty calls that
can never carry one.

`CbsBuildEvent` is in the installed header, so `CBS_ABI_VERSION` goes 4 → 5.

## What the first attempt got wrong

The obvious condition — `wait_result == 1 && WIFSIGNALED(status)` — left the
signal at 0 for a timeout, which is the case the reporting consumer most wants
named. `wait_for_child()` returns 1 only when the child was reaped without CBS
intervening; a **timeout or interrupt returns 0**, and the final `waitpid()` in
that path has already populated `status`. The information was there and the
condition discarded it.

The test is now `wait_result >= 0`: `status` is meaningful whenever the child
was reaped, including the paths where CBS sent the signal itself. Only a failed
`waitpid()` leaves it meaningless, and that yields -1 and 0.

Caught by checking all three cases rather than the exit-code one that was easy
to produce.

## Acceptance criteria

1. **An exited command reports its code** —
   `tests/observability-test.sh` asserts `"exit_status":1,"signal":0` on the
   existing failure fixture, whose `false` exits 1.
2. **A killed command reports its signal** — asserts
   `"exit_status":-1,"signal":(15|9)` on the timeout fixture. The alternation
   is deliberate: `wait_for_child()` escalates `SIGTERM` to `SIGKILL` after a
   second, so a loaded machine can legitimately report 9.
3. **A successful command carries its code too** — asserts
   `"status":0,"exit_status":0,"signal":0`.
4. **The human reporter names the cause** — asserts `FAILED (exit 1)` and
   `FAILED (signal 15|9)`.

Verified by stashing only `src/` and running the new assertions against the
pre-fix engine, where they fail.

## Observed, not changed

A timed-out command's `command-end` message is `command failed`; that it timed
out appears only in the `CPDL-E4003` diagnostic, not on the event. A consumer
can now infer a kill from `signal`, but not that CBS was the killer or why.
Naming the cause on the event is a wider change to what events carry, which
this issue did not ask for.

## Docs

Integration contract — the three numbers and what each answers, and that only
`status` existed before v0.1.118. Library guide — `CBS_ABI_VERSION` is 5, with
both bumps named. User manual — the `--events` option description.
`docs/cpdl-test-coverage.md`.
