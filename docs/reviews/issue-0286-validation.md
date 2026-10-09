# Issue #286 validation: `allow_failure` on `move` — the parser accepts it, CPDL 1.0 §4.4 does not grant it

## Decision

The specification is authoritative. `move … allow_failure` is now a validation
error:

```text
move-allow-failure.cbs:7:9: error[CPDL-E3004]: validation: move does not
  support allow_failure
```

The ticket was filed as a decision because narrowing the parser is a breaking
change in principle. Measurement removed that objection: across the 993 `.cbs`
recipes in the sibling corpus, `move` appears **3 times** and **none** of those
uses `allow_failure`. There are none in this repository's fixtures or recipes
either. Nothing breaks.

With cost at zero, the remaining arguments all point one way:

- §1's conformance rules require a conforming implementation to *reject every
  document that violates a grammar rule*, and §10 says unknown constructs must
  fail parsing or validation and "must never be forwarded to a shell or ignored
  for compatibility". Accepting an option the grammar does not define made the
  implementation non-conforming against its own normative spec.
- The asymmetry is defensible rather than an oversight. `copy` and `remove` are
  best-effort operations over paths that may legitimately be absent — dropping
  a static archive this configuration did not build. A `move` renames something
  the recipe has just produced, so tolerating its failure hides a defect
  instead of expressing intent.
- #285 had just finished demonstrating what silently tolerated filesystem
  failures cost. Widening the set of operations that can swallow a failure is
  the wrong direction for this engine.

## Shape of the rejection

Validation, not parsing, and `CPDL-E3004` — both to match the rule immediately
beside it:

```c
if (operation->kind == CBS_NODE_MOVE && operation->number)
    validation_error(validator, operation, "CPDL-E3004",
                     "move does not support tree sources");
```

`move tree` is the same shape of problem — an option this operation does not
support — and is already handled by letting the parser accept it and having the
validator refuse it, which is what produces a located message pointing at the
operation rather than a generic parse error. A different code for an identical
rule would make the diagnostic contract less predictable, which §8.2 exists to
prevent.

## No specification change

§4.4's grammar already excludes `allow_failure` from `move-operation`, and its
prose already says "A trailing `allow_failure` on `copy` or `remove`". The spec
was right; only the implementation moved.

## Acceptance criteria

1. **`move … allow_failure` is rejected** —
   `tests/fixtures/invalid/move-allow-failure.cbs` with a `.expect` file
   pinning `error[CPDL-E3004]: validation: move does not support
   allow_failure`. `parser-validation.sh` discovers fixtures automatically and
   went from 66 to 67 cases.
2. **`copy` and `remove` are unaffected** — a recipe using `allow_failure` on
   both, plus a plain `move`, still validates.
3. **Nothing in the real corpus regressed** — all 993 recipes in
   `/home/osakka/cix-recipes` still validate, re-run after the change.

## Docs

User manual §11.5 now states which filesystem operations accept
`allow_failure` and why `move` is not one of them; the manual previously
documented the option only for `run` inside `on_fail`.
`docs/cpdl-test-coverage.md`.
