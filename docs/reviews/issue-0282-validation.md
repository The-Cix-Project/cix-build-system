# Issue #282 validation: `--events` writes to a dup of stderr, so a caller cannot have both the human log and the jsonl stream

## Decision

`--events` keeps selecting the reporter on standard error. Two new options name
an independent destination for the JSONL stream:

```text
cbs build recipe.cbs --arch x86_64 --staged /tmp/ws \
    --events human --events-fd 3  3>events.jsonl
```

Both halves of the report are addressed, and the second one is the part worth
spelling out. The ask named `--events-fd N` or `--events-file PATH`, but the
reason given was that cixd *"takes the human reporter for the operator's
benefit and, as a consequence, knows nothing structured"* and wants to
*"read events on the fd and leave stderr alone"*. Moving the single chosen
reporter would not deliver that: the caller would still be choosing between
prose and records. So the destination is a **second, independent sink**, and a
build can emit both at once. That is the difference between satisfying the two
flag names and satisfying the request.

`main.c:1672`'s `dup(fileno(stderr))` is unchanged for the stderr reporter, so
its buffering stays independent of diagnostics. The destination is duplicated
the same way, so closing CBS's reporter never closes a descriptor the parent
still owns.

Resolved semantics, chosen so that both natural spellings do the obvious thing:

| invocation | standard error | destination |
| --- | --- | --- |
| `--events human` | human | — |
| `--events jsonl` | JSONL | — |
| `--events human --events-fd 3` | human | JSONL |
| `--events jsonl --events-file F` | nothing | JSONL |
| `--events-file F` (no `--events`) | nothing | JSONL |

A destination always carries JSONL: a machine channel has no use for prose.
Pairing `--events jsonl` with a destination moves the stream rather than
writing it twice, because the identical stream on two channels is never what
was wanted.

`--report` now wraps the *effective* sink rather than the stderr reporter, so
it aggregates the same events whether or not a destination is in use.

## Refusals

A bad descriptor is a usage error before the build starts, never a successful
build that silently reported nothing:

- not an integer, or negative — `CBS-E1022`, exit 2
- not an open descriptor (`fcntl(F_GETFL)` fails) — `CBS-E1022`, exit 2
- open but read-only — `CBS-E1022`, exit 2
- `--events-fd` with `--events-file` — `CBS-E1022`, exit 2
- an unopenable `--events-file` path — `CBS-E1023`, exit 3

## Acceptance criteria

1. **One build, both streams** — `tests/observability-test.sh` runs
   `--events human --events-fd 3`, then asserts the human log contains
   `run true (log:` and the descriptor contains `build-begin` and
   `command-end`. It also asserts each stream carries *only* its own form:
   no `{"version":` in the human log, no `run true (log:` in the events.
2. **`--events-file` works and moves the stream** — asserted present in the
   file, and standard error asserted empty with `--events jsonl`.
3. **A destination needs no `--events`** — `build-end` asserted in the file.
4. **`--report` still aggregates with a destination** — both the events file
   and `cbs.build-report/v1` asserted from the same build.
5. **Every refusal above** is asserted to fail with `CBS-E1022`, including the
   read-only descriptor and the combined-destination case.

The new assertions were run against the previous binary and fail there, so they
test the change rather than restating existing behaviour.

## Also recorded

The report notes that cix#388's design record (cix ADR-0275) says *"CBS emits
no phase event. That is #131, and it does not exist yet."* That is out of date:
`src/report.c` dispatches `phase-begin` and the JSONL sink emits it. #131's
substance shipped; only the destination was missing, which is what this closes.

## Not addressed

Nothing about what events contain, or the human reporter's format or default —
only where the stream can be pointed, as the report asked.

## Docs

User manual §4 build options and the worked both-streams example; integration
contract (the process-level embedder section); ADR-0008 implementation note;
`docs/cpdl-test-coverage.md`.
