# Issue #304 validation: a command's standard input is /dev/null

Filed as a decision rather than a defect, and decided with the cix maintainer
and their advisor at the owner's request. The outcome changed the implementer's
lean, on evidence.

## The question

Since v0.1.121 a recipe command's descriptor table is constructed: every
descriptor above stderr is closed between `fork` and `execve` (#301). Standard
input was the one exception — CBS had never redirected it, so a command got
whatever CBS was given. The ticket existed because that was an accident nobody
had chosen, not because CBS was known to be wrong. "Leave it" was a valid
close.

## Why /dev/null, and why in CBS

The implementer leaned toward leaving it, on the grounds that a build command
may legitimately read standard input and the benefit was theoretical. cix
supplied the fact that made it concrete, measured in a cixd build container:
**fd 0 there is `/dev/console`**. So a recipe command that reads standard input
on Cix does not fail — it *blocks*, until the daemon's stall watchdog kills the
build. The operator then gets a timeout that points away from its cause.
`/dev/null` turns that into an immediate EOF and a diagnosable failure.

That reframes the change from tidiness to a failure mode with a measured
consequence, which is why it ships.

**In CBS rather than in cixd's container.** The property is the one CBS already
enforces elsewhere: a build must not depend on what the parent happened to
hold, which is why `SOURCE_DATE_EPOCH=0` is set in every child and why the
descriptor table is constructed at all. Fixing it in the container would
correct one composer and leave the contract statement needing its caveat
forever.

**Redirected, not closed.** Closing fd 0 is worse than either alternative: the
child's first `open()` then lands on descriptor 0, and anything assuming 0, 1
and 2 are open misbehaves in ways no test here would catch. That argument came
from cix and is the clearest reason not to take the strictest option.

**Risk.** Zero of the 1969 corpus recipes read standard input. If one appears,
EOF fails it loudly where `/dev/console` hung it.

## Acceptance criteria

`tests/descriptor-hygiene-test.sh` — where the descriptor contract is already
asserted, as cix suggested — now requires the child's fd 0 to be `/dev/null`:

```text
grep -qE '^lr-x.* 0 -> /dev/null$' "$root/probe-output"
```

It fails against the v0.1.122 engine, where fd 0 is whatever the test's own
shell held.

Full gate green; all 1969 corpus recipes still validate.

## Where it is written down

The integration contract previously named standard input as the exception to
the descriptor guarantee. That sentence is now wrong in CBS's favour and has
been corrected: there is no exception.
