# Issue #313 validation: an output's own items are validated like a package's

Found while scoping #312, which adds a field to the same item set. An `output`
block (ADR-0038) may carry its own `license`, `requires` and `replaces`, and the
validator never looked at any of them.

## Measured

Each of these was accepted by v0.1.128. The package-level equivalent is
rejected.

| In an `output` | Result before | Package equivalent |
| --- | --- | --- |
| `license ""` | valid | `CPDL-E3004` |
| `license "a\nb"` | valid | `CPDL-E3004` |
| two `license` declarations | valid | `CPDL-E3002` |
| `requires { runtime { library "" } }` | valid | `CPDL-E3004` |
| `replaces { package "" }` | valid | `CPDL-E3004` |
| two `requires` or two `replaces` | valid | `CPDL-E3002` |

## Why the newline is not cosmetic

The artifact manifest is line-oriented and `m KEY VALUE` lines are written
verbatim, so a license containing a line break writes a second line:

```text
license "MIT\nm injected value"          (inside an output)
$ cbs list a-1-1-x86_64.cixpkg
metadata license MIT
metadata injected value
```

The artifact verified. A recipe author could therefore forge any metadata key in
an output's artifact, `build_fingerprint` included, which ADR-0038 relies on to
say every output came from one build, and the ABI a provider declares in #312,
which cixd will key on. The package-level rule rejects `\r` and `\n` for exactly
this reason; the output copy never received it.

## Cause

`validate_package` validates a package's items through a switch, but its `case
CBS_NODE_OUTPUT` checked only the name, its uniqueness and `files`. The output's
other children were never visited. #299's record says an output "carries its own
`requires`, `license` and `replaces`" and never says they were checked, and all
three invalid output fixtures were about names, selectors and `files`. The
omission survived because nothing asserted it either way.

## Decision

Apply the existing rules instead of writing a second copy. The license rule, the
`replaces` block rules and `validate_requires` are now functions called from
both the package and the output, and an output may carry at most one of each,
through one helper that the next item added to an output can reuse. No new code
was needed: `CPDL-E3004`, `E3001` and `E3002` already mean exactly this, so the
registry is unchanged.

The package-level behaviour is unchanged, which the existing 74 cases confirmed
after the extraction and before any output rule was added.

## Acceptance criteria

Five invalid fixtures, each with an `.expect` file naming the diagnostic: an
empty license, a license with a newline (the injection), a duplicate license, a
`requires` with an empty name, and a duplicate `replaces`. One valid fixture
carries one of each item. The parser suite goes from 74 to 80 cases, and the
invalid ones fail against the v0.1.128 engine.

The specification and the manual now say that each item is at most once and
checked as on the package, and why a license may not contain a line break.

## Not done, on purpose

The manifest writer still writes whatever value it is handed. Refusing a line
break in the one function that writes `m` lines would be defence in depth, but
there is no such function: the writers are several `fprintf` calls, and
composer-supplied values such as the recipe path reach some of them. Changing
that is a larger decision than this defect, so it is recorded here and not
quietly skipped.
