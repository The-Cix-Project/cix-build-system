# Issue #295 validation: `materialize` had no failure tests despite being the third most-used operation

## The gap, measured

Construct usage across the 1010-definition corpus put `materialize` third —
3219 uses, behind `run` (3881) and `require` (3763), ahead of `copy` (1606),
`mkdir` (1413) and `write` (296). Its entire representation in the suite was
one success-path line in `tests/fixtures/execution/extract.cbs`, and the
coverage matrix folded it mid-sentence into the `extract` row, which read as
covered.

Two of its four refusals are the operation's security contract. CPDL 1.0 §4.5:

> It copies that exact regular file into the confined build filesystem; it
> cannot read an arbitrary cache path or follow a source symlink.

Nothing asserted either held.

## What was added

Unit-level assertions in `tests/fs-test.c`, which builds an execution context
directly and can therefore reach refusals the CLI cannot — a declared source
that is a symlink is not constructible through `cbs build`, because sources
arrive from the digest-verified cache.

The success path is asserted first, so the refusals cannot pass vacuously, then
five refusals with their diagnostics captured:

1. **an undeclared reference** — `$source.nosuch` resolves to nothing;
2. **an undeclared path** — a readable regular file that is simply not a
   declared source: the "cannot read an arbitrary cache path" half;
3. **a source symlink** — declared, and pointing at the declared source, which
   is the point: the check is on the source itself rather than on what it
   resolves to, so a link cannot be laundered through a legitimate target;
4. **a declared source that is not a regular file**;
5. **a destination leaving the confined roots**, with the escaped path asserted
   absent afterwards.

All five are required to refuse, and `CPDL-E4004` is required in the captured
output.

## Verified

Every refusal already held — this closes a test gap, not a defect. Confirmed by
removing the `declared` check and the `S_ISLNK` guard in a scratch build: the
new assertions fail there and pass with them in place, so they are load-bearing
rather than decorative.

## Observed, not changed

`cbs_execute_materialize()`'s failure label reports `operation->value` for
every refusal:

```c
failure:
    fs_error(operation, context, operation->value,
             "verified source cannot be materialized");
```

So a destination that escapes the roots is reported against the **source**
reference — `` `$source.m` `` — when the destination is what was refused. This
is the same family as #287, where a failing `symlink` named its target rather
than the link path. Not folded in here: this issue is a test gap, and changing
diagnostic operands is a separate concern with its own regression surface.

## Docs

`docs/cpdl-test-coverage.md` now gives `materialize` its own row naming the
refusals, instead of folding it into `extract`.
