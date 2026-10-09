# Issue #287 validation: filesystem failures report ELOOP for a non-directory parent, and `symlink` names its target instead of the link path

## Two defects, one diagnostic

Both became reachable for `write` only once #285 stopped swallowing its
failures. Neither is a regression; they were always there.

### 1. The operating-system error was invented

`safe_parents()` collapsed two unrelated conditions:

```c
if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
    errno = ELOOP;
```

So a parent that was merely a regular file reported
`errno=40 (Too many levels of symbolic links)` with no symbolic link anywhere
in the path.

The consequence was worse than an inaccurate string. A genuine escaping-symlink
refusal produced the **identical** message, so an operator could not tell a
confinement refusal from an ordinary path collision — two findings that warrant
completely different responses, separated by nothing.

Now split: a symbolic link in a parent position keeps `ELOOP`, because that is
the confinement refusal and the name fits. Anything else that is not a
directory reports `ENOTDIR`.

### 2. A failed `symlink` named the wrong operand

Every filesystem operation names the path it acts on in `value` — except
`symlink`, where `value` is the link's target text and `second_value` is the
path being created. The failure path reported `operation->value` for all kinds,
so a `symlink` that could not be created named a string that was never touched.

`operation_target_path()` now returns the acted-on path per kind, and the
failure path uses it. `filesystem_failure_path()` already mapped `symlink` to
`second_value` for its `ENOENT` parent-naming case, so the two agree now
instead of disagreeing by errno.

## Evidence

At `1e686aa` and with this change:

| case | before | after |
| --- | --- | --- |
| `write` under a regular-file parent | `errno=40 (Too many levels of symbolic links)` | `errno=20 (Not a directory)` |
| `write` under a symlinked parent | `errno=40` | `errno=40` — unchanged |
| failed `symlink` | names `` `the-target` `` | names `` `${dest}/blocker/the-link` `` |

The middle row is the point of the first change: it must not move, or the fix
would have traded one indistinguishable pair for another.

## Acceptance criteria

1. **The two are distinguishable** — `tests/fs-test.c` asserts
   `Not a directory` for a write whose parent is a regular file, and
   `Too many levels of symbolic links` for the pre-existing
   `${dest}/linked-parent/escaped` case. That second assertion was previously
   `NULL` (code only); it now pins the text, so a future change cannot quietly
   merge them again.
2. **`symlink` names the link path** — asserted by fragment on the resolved
   `second_value`, which fails if the reporting reverts to `value`.
3. The write-failure and `allow_failure` assertions from #285 are unchanged and
   still pass.

Verified by restoring both old behaviours in a scratch build: the new
assertions fail there and pass with the fix.

## Checked for collateral

No test or fixture pinned the old text: the only `CPDL-E4004` assertions in the
suite were code-only, and the `E4004` match in `tests/cli-contract-test.sh` is
`CIXPKG-E4004`, a different namespace. The two mentions of `ELOOP` in
`docs/reviews/issue-0285-validation.md` are a historical record of the state at
that issue's close and are deliberately left as written.

## Deliberately not widened

`copy` and `move` still name their source selector on failure, which is right
for a missing source but not for a destination failure. Deciding which operand
a two-path operation should name needs a view on the reporting contract rather
than a third special case, and the reporting ticket raised only `symlink`.

## Docs

User manual troubleshooting — the two errno values and what each means, since
one is a policy refusal and the other is a mistake;
`docs/cpdl-test-coverage.md`.
