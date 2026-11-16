# Issue #315 validation: an output's items are a checked contract

Raised by the owner after #314, as a question: why do we keep discovering
omissions that are so simple? This is the answer to that question, not another
instance of it.

## The recurrence

Three omissions in two weeks, all in the same place. #313: the validator never
visited an output's own `license`, `requires` and `replaces`, and a license
holding a newline injected a manifest line. #314: `explain` accepted an output's
`replaces` and dropped it from `outputs[]`. And an output's `license` is also
absent from `outputs[]`, found while scoping #314. `abi` (#312) reached all four
consumers only because the advisor enumerated them before it was started.

## Root cause

An output's item set is an implicit contract with four independent consumers:
the parser, the validator, `explain` and the artifact manifest. Each keeps its
own `if kind == X` chain and nothing checks that they agree. The only thing
keeping them aligned was someone remembering.

Measured behaviourally, per item, on an output declaring that item alone:

```text
          validator   explain entry   artifact manifest
abi          yes          yes              yes
license      yes          NO               yes
replaces     yes          yes (#314)       NO
requires     yes          yes              NO
```

Not every cell should be yes. No artifact records `requires` or `replaces` at
any level, by design, so "every item in every consumer" is the wrong rule, and
the first suggestion made for this ticket (grep each consumer's source for the
item's node name) would have enforced it. It also checks that a name appears and
not that the behaviour exists, so a comment containing the name would pass. The
right rule is that **every cell is a decision**, written down once, with a
reason wherever the answer is no, and checked against what the engine does.

## Decision

`tests/output-items-test.sh`, in the gate. The optional item list is derived
from `parse_output`, the authority, minus `files` (structural) and `glob` (a
selector modifier). Each item needs a row giving a valid declaration, the
pattern `explain` must show on that output's own entry, the line the artifact
must record, and an invalid value that must be refused. The rows are checked
against behaviour: the test declares the item in a recipe, validates it, builds
it, runs `explain` and `list`, and tries the invalid one.

A `-` in the explain or manifest column means that consumer does not carry the
item, and the row must then say why, so an omission can only be a decision. It
fails closed in both directions: an item with no row names itself and says what
to decide, a row for an item the parser no longer accepts fails, and fewer than
five extracted items fails rather than comparing nothing.

The one cell that is not yet a decision, `license` in `explain`, is an explicit
exemption naming the open question. It was put to the cix maintainer, so the gap
is visible in the test instead of silent. When they answer, the exemption is
either removed (and `explain` gains the field) or kept with the answer as its
reason.

## Acceptance criteria

The test passes on the current engine. Run against engines built from the tags
that had the defects, it fails exactly where it should:

- `v0.1.129`: `explain` does not carry `replaces` on the output's own entry,
  which is #314;
- `v0.1.128`: an empty `license`, an empty `replaces` package and an empty
  `requires` library are each accepted in an output though refused on the
  package (#313), `replaces` is missing from `explain` again, and `abi` is
  unknown.

So it would have failed on the day each defect was introduced. On a full copy of
the tree, a new item added to `parse_output` with no row fails with a message
naming it, and renaming the parser's helpers fails closed. The exit codes were
read from the script itself: an earlier run printed `cut`'s status through a
pipe.

## Rejected

A single table generating all four consumers would make the omission
structurally impossible. It is also a large refactor of the parser, the
validator, the artifact writer and `explain` for five items. If outputs grow
more items, revisit it; the test keeps them honest until then.

## Not done

The test covers an output's items only. The package-level item set has the same
shape and the same risk, and is the obvious next place to look. It has not shown
a defect yet, so it was not widened speculatively.