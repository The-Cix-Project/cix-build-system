# Issue #302 validation: ask the caller about the caller's descriptor

Reported by the cix maintainer session: v0.1.121 still failed CBS's own
`make test` on Cix with the same line as #301, and their hypothesis — offered
explicitly as unmeasured — was right.

#301 fixed the descriptors CBS leaks to its children. This is the other
direction: CBS validated `--events-fd N` against **its own** descriptor table,
after it had already opened something.

## Measured

```text
fds 3-6 occupied in the parent, 9 closed:  refused   (correct)
fds 3-7 occupied in the parent, 9 closed:  refused   (correct)
fds 3-8 occupied in the parent, 9 closed:  ACCEPTED  (wrong)
```

The open is `event_fd = dup(fileno(stderr))`, the stderr reporter's own stream,
which ran *before* `parse_event_descriptor`. `dup` returns the lowest free
descriptor; where the parent left 3..8 occupied that is 9. So
`fcntl(9, F_GETFL)` was asked about CBS's own dup of stderr, the usage check
passed, and the event stream was written there — which is also why the JSONL
appeared on stderr in the reproduction: fd 9 *was* stderr.

Re-confirmed with 3..8 and 10..14 occupied and 9 closed, to show it is the
lowest-free-number rule rather than an artifact of how the shell was driven.

## Decision

Validate the caller's descriptor before CBS opens any of its own: immediately
after the `--events-fd`/`--events-file` exclusion check, before the reporter's
`dup`. "Did the caller hand me an open, writable descriptor" is a question
about the caller's descriptor table and can only be asked there.

The comment above `parse_event_descriptor` already claimed the check was made
"up front so a mistyped or closed descriptor is a usage error". The claim was
right about the intent and wrong about the code; now they agree.

This is a correctness fix and not only a test fix. A composer passing a stale
`--events-fd` number that happened to collide with one of CBS's own descriptors
had its event stream written into that descriptor instead of being refused.

## Why this escaped the gate, and why it is self-limiting

The reporter's account is correct. The outer engine building this one leaks
fds 3..6 — #301, fixed only in v0.1.121 — so `make test` and the inner `./cbs`
start with 0..6 occupied, and `make` and the shell push that to 8. On a clean
shell CBS's own opens land at 3..5 and the assertion is asked of a genuinely
closed descriptor, so the gate passed here every time.

It is a bootstrap trap, as they called it, but a self-limiting one: the
assertion runs against the engine *being built*, so an engine carrying this fix
validates correctly however many descriptors its parent leaked. Every engine up
to v0.1.121 fails this way; v0.1.122 does not, whichever engine builds it.

## Acceptance criteria

`tests/observability-test.sh` asserts the refusal with 3..8 occupied and 9
closed, in a subshell so the rest of the suite is unaffected by the
redirections. It fails against the v0.1.121 engine.

The clean-shell assertions stay at fd 9 rather than moving to a high number.
The reporter offered that as option (b), and it was declined for the reason
they would want: it makes the suite pass while leaving the defect in place. The
test closing fd 9 itself (#301) is still right, because any leaking parent
breaks the assumption — but on its own it was not enough, since the number it
closes can be reopened by CBS before the check.

Full gate green; all 1969 corpus recipes still validate.
