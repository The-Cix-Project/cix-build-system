# Issue #3 validation: direct `run` execution

- Issue: `#3 Implement run: argument vectors with no implicit shell`
- Date: 2026-08-28
- Result: Pass
- Implementation commit: pending repository commit

## Delivered boundary

`src/exec.c` implements the single production process-execution path for CPDL
`run` AST nodes. It accepts an explicit, immutable execution context and:

1. resolves typed CBS values and `${...}` interpolation;
2. creates exactly one argument per CPDL argument node;
3. constructs a deterministic environment plus command-local bindings;
4. resolves the executable without `execvp` or ambient `PATH` inheritance;
5. creates an isolated process group;
6. calls `execve` directly;
7. waits for and reaps the child; and
8. reports status, signal, timeout, resolution, and process failures through the
   CPDL runtime diagnostic contract.

The executor is an API consumed by the existing AST. No second parser, temporary
CLI command, shell adapter, or parallel execution implementation was introduced.
Issue #20 will connect this API to the final CBS command surface.

## No-shell proof

The production source contains no call to:

```text
system
popen
execvp
execlp
wordexp
```

CBS resolves the executable itself and calls `execve(executable, argv, envp)`.
The shared `cbs_is_forbidden_executable` policy is used by both static validation
and runtime validation after interpolation. It rejects command interpreters and
`env` rather than offering an escape hatch.

Verified upstream scripts remain executable through their own kernel-recognized
interpreter header; CBS never places recipe text in a shell command string.

## Argument and environment integrity test

The integration fixture passes the following as distinct arguments:

```text
space value
"double quotes"
$
*
;
'single quotes'
back\slash
<empty string>
jobs=${jobs}
```

The TCC-built child process compares its received `argc` and every `argv` byte
against compiled expected values. Any field splitting, quote removal, globbing,
variable expansion, command interpretation, or empty-argument loss changes the
child's exit status and fails the parent test.

The same child verifies a command-local environment value containing spaces,
`$`, `*`, `;`, and both quote characters. A second command asserts that the
binding is absent, and the parent confirms its own environment was never
modified.

## Process-result tests

The integration suite verifies:

- a nonzero status explicitly selected with `expect exit 23` succeeds;
- a nonzero status under the default expectation fails with `CPDL-E4001`;
- termination by signal fails with `CPDL-E4002`;
- a timed-out process group is terminated, reaped, and fails with
  `CPDL-E4003`; and
- command-local environment state does not leak between runs.

CBS returns its structured success/failure result rather than replacing it with
the raw child status.

## Deterministic environment

The child does not inherit the invoking process environment. The initial
execution environment contains the CBS-controlled path:

```text
PATH=/usr/bin:/bin
```

Validated command-local bindings are then added or replaced. Executable lookup
uses that constructed path and calls `execve`; `execvp` is intentionally absent.
Future sandbox policy may supply a different deterministic base environment
through the same execution boundary without creating another executor.

## Timeout behavior

When a timeout expires, CBS sends `SIGTERM` to the complete process group and
polls for up to one second. If the group leader has not exited, CBS sends
`SIGKILL` and performs a blocking reap. The timeout test confirms the runtime
does not leave the child running.

## Verification performed

Normal gate:

```text
make clean
make
make test
```

All production and test code was compiled by TCC with:

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

The execution integration binary was then rebuilt from source with TCC bounds
instrumentation:

```text
tcc -b ... -o /tmp/cbs-exec-bounds/exec-test
```

The same direct-execution suite passed under the instrumented binary.

## Result

- Warning-clean TCC build: Pass
- Existing parser/validator regression suite: Pass (49 cases)
- Byte-exact argument test: Pass
- Command-local environment and non-leakage test: Pass
- Expected/default status tests: Pass
- Signal propagation test: Pass
- Timeout/process-group/reaping test: Pass
- TCC bounds-instrumented execution suite: Pass
- Shell API audit: Pass

Issue #4 can implement filesystem operations against the same AST and execution
context without altering `run` semantics.

