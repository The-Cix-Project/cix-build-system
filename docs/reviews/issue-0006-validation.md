# Issue #6 validation: `on_fail` and `allow_failure`

- Issue: `#6 Implement on_fail and allow_failure semantics`
- Date: 2026-08-28
- Result: Pass
- Implementation: the commit containing this review

## Delivered boundary

`src/runtime.c` introduces the single block orchestration path for validated
phase and `cd` AST blocks. It dispatches to the existing process, filesystem,
and edit/assertion executors rather than duplicating their mechanics.

The block executor owns only cross-operation behavior:

- source-order execution;
- block environment bindings;
- nested directory context;
- stop-on-first-primary-failure;
- associated `on_fail` execution;
- diagnostic failure subordination; and
- per-run `allow_failure` continuation.

No operation implementation moved into the orchestrator and no second command,
filesystem, glob, edit, or assertion path was introduced.

## Primary failure preservation

Each operation executes with its standard diagnostic captured. On the first
failure, the block executor:

1. emits that original diagnostic unchanged as the primary error;
2. stops the normal block immediately;
3. executes only the final `on_fail` associated with that block;
4. emits any diagnostic-operation failure as `CPDL-N4001`; and
5. returns failure regardless of every diagnostic result.

The subordinate note includes the diagnostic operation's original first line,
including its stable error code and process status. It also states explicitly
that the original failure remains primary. A diagnostic failure therefore
cannot replace, consume, or reinterpret the triggering failure.

The `on_fail` node is identified before normal execution begins. This matters
because the normal operation loop stops at the first failure and must not depend
on reaching the final AST child to discover its handler.

## `allow_failure`

`allow_failure` is inspected only on the individual `run` node being executed
inside `on_fail`.

- A failing diagnostic run with `allow_failure` emits a subordinate note and
  permits the next diagnostic operation to run.
- A failing diagnostic run without it emits a subordinate note and stops the
  diagnostic block.
- The setting is not copied to later operations.
- It never changes the block's primary failure result.

Static validation continues to reject `allow_failure` outside `on_fail` and
duplicate uses on one `run`.

## Environment and directory scope

The block executor carries an immutable execution-context copy. Block-level
environment declarations append scoped bindings used by later operations and
nested blocks. Leaving the block frees only bindings created in that block and
restores the caller's context.

`run` now constructs its deterministic base environment, applies scoped block
bindings in source order, then applies command-local bindings. This gives the
most local declaration precedence without inheriting the invoking process
environment.

Nested `cd` resolves through the same confined path implementation as issue #4,
requires a real directory rather than a final symlink, and changes only the
nested execution-context copy.

## Acceptance proof

The integration fixture triggers a normal build failure with process status 17.
Its `on_fail` block then contains:

1. a status-18 diagnostic run with `allow_failure`;
2. a successful marker operation that verifies the scoped block environment;
3. a status-19 diagnostic run without `allow_failure`; and
4. a second marker operation that must not execute.

The test captures the complete block diagnostic stream and verifies:

- the status-17 `CPDL-E4001` primary error occurs first;
- status 18 is recorded in the first `CPDL-N4001` note;
- the continuation marker exists;
- status 19 is recorded in the second `CPDL-N4001` note;
- the stop marker does not exist; and
- `cbs_execute_block` still returns failure.

A separate successful phase proves that an `on_fail` marker is not created when
all normal operations succeed.

## Verification performed

The normal gate is:

```text
make clean
make
make test
```

All production and test translation units compile with TCC using:

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

The failure-orchestration binary is also rebuilt from source with TCC `-b`
bounds instrumentation and run against the same fixture.

## Result

- Warning-clean TCC build: Pass
- Existing parser, process, filesystem, edit, and assertion regressions: Pass
- Original failure retained as primary: Pass
- Failing diagnostics recorded as subordinate notes: Pass
- Per-operation `allow_failure`: Pass
- Non-inheritance and diagnostic stop behavior: Pass
- Successful block skips `on_fail`: Pass
- Scoped deterministic environment: Pass
- TCC bounds-instrumented suite: Pass

Issue #7 can add package identity to the immutable execution context without
altering the block or failure model.
