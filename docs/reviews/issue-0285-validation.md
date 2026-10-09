# Issue #285 validation: every `write` failure is reported as success, shipping an artifact silently missing the file

## Cause

`CbsNode` carried three different meanings on one `int`:

- `flag` / `second_flag` hold the **token kinds** of `value` / `second_value`,
  so interpolation and validation can treat a block string differently from a
  quoted one. `src/parser.c`'s `write` branch sets
  `node->second_flag = value->kind`.
- `second_flag == 1` *also* meant **`allow_failure`** for `copy`, `move` and
  `remove`.
- `second_flag == 1` *also* marked a `tools { compiler alias … }` node.

`cbs_execute_filesystem()` ended its failure path with
`return operation->second_flag ? 1 : 0`, and `src/runtime.c` read the same
field the same way. A `write` whose text is a quoted string has
`second_flag == CBS_TOKEN_STRING` (2), so **every** `write` failure was
converted to success.

The failure was silent rather than merely non-fatal because
`execute_captured()` redirects stderr into a buffer and the block executor
prints that buffer only when the operation fails. A swallowed failure
therefore discarded its own `CPDL-E4004` on the way out: no diagnostic, exit 0,
and a verified artifact missing the file.

## Decision

Give `allow_failure` its own field on `CbsNode` rather than making the
allow-failure check kind-aware.

A kind-aware check would have fixed the symptom and left the overload in place
for the next operation that stores a token kind in `second_flag` — the field
would still have had three meanings, and the trap would still have been armed.
With a dedicated field, `flag` and `second_flag` mean exactly one thing
(the token kind of `value` and `second_value`), and the operations the grammar
grants `allow_failure` set it explicitly. `src/cbs.h` is repository-private and
not installed, so adding a field is not an ABI change.

Migrated together, so no reader is left on the old field: the two parser sites
that set it, `clone_node()` (otherwise an `each`-expanded `copy … allow_failure`
would silently lose the flag), the optional-glob no-op path in `src/fs.c`, the
failure path in `src/fs.c`, and the filesystem branch in `src/runtime.c`. The
`tools` marker and the token-kind readers are untouched.

Removing `second_flag = 1` from `copy`/`move`/`remove` cannot change validation:
`validate_secondary_value()` only special-cases `CBS_TOKEN_BLOCK_STRING` (3), so
both the old `1` and the new `0` validate identically.

## Scope checked

`write` was the only affected operation, confirmed by reading and by test.
`is_filesystem()` covers `mkdir`, `copy`, `move`, `remove`, `symlink`, `write`
and `chmod`; of those only `write` stores a token kind in `second_flag`.

The reporting ticket asked for `symlink` to be checked. It was never affected:
its parser branch sets no flags, so a failing `symlink` returned 0 before this
change and still does — verified against both binaries.

## Evidence

```cbs
write "${dest}/blocker" "i am a file\n" chmod 0644
write "${dest}/blocker/hello" "cannot possibly work\n" chmod 0644
write "${dest}/after" "later\n" chmod 0644
```

The second `write` cannot succeed: its parent is a regular file.

| | v0.1.108 | this change |
| --- | --- | --- |
| diagnostic | none | `CPDL-E4004` naming `${dest}/blocker/hello` |
| exit status | 0 | 3 |
| artifact | written, missing `hello`, verifies | not written |

## Acceptance criteria

1. **A failing write fails** — `tests/fs-test.c` asserts `CPDL-E4004` and a
   false result for a `write` under a regular-file parent, with
   `second_flag` set to `CBS_TOKEN_STRING` and then to
   `CBS_TOKEN_BLOCK_STRING`: both kinds a write can carry are covered, so the
   test fails if either value is ever read as a flag again.
2. **Nothing is left behind** — the target path is asserted absent afterwards.
3. **`allow_failure` still works** — a glob `remove` with `allow_failure` that
   matches nothing is asserted to return success, so the fix cannot have been
   made by simply ignoring the flag.

Verified by restoring the old `return operation->second_flag ? 1 : 0` in a
scratch build: the new assertions fail there and pass with the fix.

## Observed, not changed

Two adjacent issues were noted and deliberately left alone:

- `src/parser.c` accepts `allow_failure` on `move`, but CPDL 1.0 §4.4 grants it
  only to `copy` and `remove`. The implementation is more permissive than the
  grammar. Behaviour is preserved here rather than narrowed in a bug-fix commit.
- `safe_parents()` sets `ELOOP`, so a write whose parent is a regular file now
  reports `errno=40 (Too many levels of symbolic links)`. The refusal is right
  and is now visible, but the operating-system error is not what happened. A
  failing `symlink` also names its target rather than the link path it could not
  create.

## Docs

`docs/cpdl-test-coverage.md`. No user-facing behaviour is documented differently:
the spec already required that every operation either succeeds or returns one
structured failure (§7), which is what this restores.
