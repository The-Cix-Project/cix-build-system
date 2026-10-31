# Issue #309 validation: run the documentation's examples, and check the reviews

#306 turned the claims a script can compare into a gate and #308 swept the prose
by hand. Both stopped short of the one kind of claim that is cheap to check and
was not: the examples. The question that prompted this was whether the gate's
blind spot could be fixed or only complained about.

## What was measured, before building anything

The documentation holds 22 `cbs` fences, 21 `sh` fences and two `cpdl` fences.
Four `cbs` fences are complete recipes; the rest are fragments. Run through
`cbs validate`:

| Where | Example | Result |
| --- | --- | --- |
| `user-manual.md` section 2 | "The first recipe" | **invalid** |
| `user-manual.md` section 4 | `example` | **invalid** |
| `spec/cpdl-1.0.md` | "the complete shape" | **invalid** |
| `spec/cpdl-1.0.md` | `openssh` | valid |

All three failures are `CPDL-E3001: package requires exactly one artifact format
declaration`. `format "cixpkg"` is mandatory (ADR-0030) and each example omits
it. The manual's first recipe has been uncopyable since it was written on
2026-09-10, and the normative specification's own demonstration of the whole
shape was invalid too. The manual's "Important rules" list for that recipe also
never mentioned `format`, so a reader could not discover the requirement from
the page.

My own #308 sweep declared the manual done without running any of its examples.

## Decision

**Run what can be run, opt-in.** A block fenced ` ```cbs check ` is extracted
and given to `cbs validate` by `tests/docs-examples-test.sh`. Untagged blocks
are illustrative and skipped deliberately: 19 of 22 `cbs` blocks are fragments
such as `build { ... }`, and a manual that could only show runnable examples
would be worse for the reader. Opt-in also keeps the check honest about what it
covers.

**The extractor fails closed**, because an extractor is a new place for the
failure it exists to prevent. Finding no tagged block is a failure, not a pass
over nothing. An unknown tag such as a misspelt `chek` is a failure, since a
typo must not silently opt a block out. The `cpdl` spelling of the same language
is a failure too, so one fence name is checked and not two. ADRs and validation
records are not extracted: they are history and are never rewritten.

**Attestations are checked to exist.** ADR-0004 requires a native-helper review
at every release and none was logged for thirteen releases, because nothing
looked at the log. `docs-contract-test.sh` now fails if the newest entry in
`docs/native-helpers.md` does not name the current `VERSION`. That does not
judge the review; it makes skipping the human step turn the gate red. The cost
is that each release now needs a real, if short, review entry, which is what the
ADR already required.

## Acceptance criteria

`tests/docs-examples-test.sh` is in the gate and validates four tagged examples.
Each fail-closed property was exercised on a scratch copy of the documentation
rather than assumed:

1. removing `format` from the manual's first recipe fails, naming
   `docs/user-manual.md:65` and the diagnostic;
2. stripping every tag fails, instead of passing over nothing;
3. a misspelt tag fails;
4. a `cpdl` fence fails;
5. a tagged block indented inside a list item is still extracted and validated;
6. and an invalid indented block is caught, not skipped.

The attestation check was exercised by setting `VERSION` to a release that has
no review entry, and fails naming the file and the ADR.

The three broken examples are fixed and the first recipe's rules list now states
that `format "cixpkg"` is required.

Full gate green; all 2016 corpus recipes validate.

## Deliberately not done

- **`sh` blocks.** 14 of 21 call `cbs`, but most use placeholder paths, and a
  doctest harness for shell is a framework this repository does not want.
  Revisit only if one rots.
- **Wrapping fragments in a scaffold** so they can be validated: more machinery
  for the blocks least likely to be wrong in a checkable way.
- **A re-read log for the guides.** Interpretation does not go stale the way
  measurements and examples do.

## What this still does not catch

Absences, such as the manual never having had an `output` section, and anything
that is true only of how a sentence reads. Facts that change underneath a
document, such as the corpus size, are handled by dating them and pointing at
the command that re-measures them, not by this.

The first run is the argument for the whole exercise: not a hypothetical, but
three of four complete examples invalid, the manual's first recipe among them.
