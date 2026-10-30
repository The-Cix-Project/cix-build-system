# Issue #301 validation: a command inherits no descriptor beyond stderr

Reported by the cix maintainer session against v0.1.120: CBS's own `make test`
failed when CBS was built by CBS on Cix, stopping `cix-builder` and
`cix-hosttools` in `[check]` with `` `--events-fd 9` was accepted ``.

The report was right, and its diagnosis of the mechanism was right. Two halves,
of which the second is the defect.

## The test assumed its environment

`tests/observability-test.sh` asserted that `--events-fd 9` is refused, on the
assumption that fd 9 is closed. That holds on a clean shell and nowhere else.
Reproduced both ways locally: with fd 9 open in the parent the v0.1.120 test
prints exactly the reported line, and with it closed it passes. The assertion
was testing its own environment.

It now closes the descriptor itself before asserting, so the precondition is
established rather than assumed. A test that needs a closed descriptor cannot
pick an unused number and hope.

## CBS leaked its descriptors into every recipe command

`exec.c` builds the child's argv and environment from scratch and rejects a
recipe `PATH` override, and the user manual says the child environment is
constructed by CBS and never inherited. The descriptor table did not follow
that rule: the child `dup2`s onto 0, 1 and 2, then `execve`s without closing
anything above 2.

Measured with a recipe running `ls -l /proc/self/fd/`, which reproduced the
reporter's probe:

```text
0 -> /dev/null
1 -> pipe:[166294046]
2 -> /tmp/#562441 (deleted)
3 -> pipe:[166294046]            same pipe as CBS's stdout
4 -> /tmp/.../events.jsonl       the --events-file, open for writing
5 -> /tmp/#562441 (deleted)      same file as CBS's stderr
6 -> pipe:[166294046]
```

With `--events-fd 7` the child inherits the composer's own event channel, at
the number the composer chose.

**What was and was not demonstrated.** The descriptor is present and writable,
so a recipe command can open `/proc/self/fd/4`. An attempt to forge an event
through it did not succeed: CBS writes the stream through its own `FILE*`, and
that offset overwrites what a child appends, so a `dd … oflag=append` and a
`cp` onto the descriptor both left the stream with seven valid JSONL lines and
nothing forged. Shells are a forbidden executable, so the direct `>&4` route is
not reachable from a recipe at all.

So this is recorded as a contract violation and a hardening gap, not a
demonstrated forgery. Claiming more than was measured is the thing the project
does not do. The fix is warranted on the contract alone, and it is also what
made the nesting failure possible: a nested `cbs` inherits whatever the outer
one held, so no descriptor number is free by assumption.

## Decision

The child's descriptor table is constructed, like its argv and environment.
After the `dup2` calls and before `execve`, every descriptor above stderr is
closed.

Enumerated from `/proc/self/fd` rather than looped to the limit: the soft
`RLIMIT_NOFILE` on the measured host is 1048576, and a million `close()` calls
per command is not acceptable where a build runs thousands of commands. When
procfs is absent the fallback loops to 4096, the ceiling `apply_child_limits`
installs, so the behaviour degrades to correct-and-slower rather than to
silently-leaking.

Closing in the child rather than opening with `O_CLOEXEC` is deliberate. CBS
cannot set `O_CLOEXEC` on a descriptor it did not open, and two of the leaked
descriptors — the pipes at 3 and 6 — came from the composer. Sanitising the
table covers every descriptor, including ones a future call site forgets to
flag, and it is one mechanism rather than a flag that must be remembered at
each `open`.

## Acceptance criteria

`tests/descriptor-hygiene-test.sh`, new and wired into the gate: with two
descriptors leaked from the shell and an `--events-file` passed, a recipe
command sees no descriptor above stderr; the events file and both leaked paths
appear nowhere in the child's table, each named separately so a regression says
which leaked; the build still succeeds, the artifact verifies, the event stream
still carries `build-end`, and neither leaked descriptor was written to. It
fails against the v0.1.120 engine.

Verified directly: with fds 8 and 9 open in CBS's parent, a recipe command's
table is `0 1 2` and the one descriptor `ls` opens for the directory it lists.

Full gate green; all 1969 corpus recipes still validate.

## Not changed

The child still inherits CBS's **stdin**, which CBS has never redirected. That
is pre-existing and a separate decision — a build command reading standard
input may well be intended — so it is not folded into a fix for a reported
regression. Flagged to the reporter rather than decided unilaterally.
