# Issue #312 validation: a package declares the ABI it offers

Asked for by the cix maintainer on behalf of cix ADR-0373 (cix#672). On
2026-11-03 hibr 0.99.432 moved its module ABI from 16 to 17. longwire builds its
module against `hibr.h`, its installed build was ABI 16, nothing rebuilt it, and
a container crash-looped for hours on `abi 16, shell expects 17`. cixd will
record the ABI each build was built against and hold a provider's roll in an
image whose consumers were built against another, and all of that keys on the
provider declaring the number, because only the provider knows when it changes.

## What ships

A package may declare `abi "N"`, as a typed field and not a `metadata` pair, for
the reason cix#276 and cix ADR-0322 gave: an author's declaration of a fact
other software depends on should be checked, not carried blind. It is carried
everywhere `license` is, which is the repository's one chain for a fact a
package declares about itself:

- parser, validator and plan metadata, in the order the language-construct
  checklist gives;
- `cbs explain --json`, as a top-level `"abi"` and as `"abi"` on each `outputs`
  entry, null when absent, so a consumer reads one shape;
- the artifact manifest, as `m abi`, so an installed provider says what it
  offers. This was the advisor's recommendation and the incident was about
  installed artifacts, not recipes. cix confirmed it reads `explain` only, for
  the better reason that the hold has to be decided before the provider's
  artifact is fetched, and said to keep the manifest line anyway: an artifact
  describing itself is right.

An `output` may declare its own `abi`. The main entry reports the package's, and
**an output's is never inherited**: one that declares none reports null even
when the package declares 17. That is `license`'s rule today and cix asked for
it by name, since cixd reads the field of the exact entry it composed.

## Decisions

**Validation reuses `version`'s rule** (non-empty, no `/`, no whitespace)
through one shared predicate, not a second copy that could drift. An ABI is
version-like, and whitespace includes `\r` and `\n`, so a value cannot carry a
line break into the manifest. No new diagnostic code: `CPDL-E3004`, `E3002` and
`E3003` already mean exactly this, so the registry is unchanged.

**It sits at the same rank as `license`**, after `format`, so `abi` follows the
identity items and a declaration after a phase is `CPDL-E3003`.

**No ADR.** This applies the typed-field decision of cix#276 to one more field,
and adds no mechanism.

**`inspect` is unchanged.** I first added an `abi` line to the human summary and
the existing CLI contract test failed: that verb's output is asserted to be
exactly two lines, and the ticket asks only for `explain`. Adding a line to a
stable verb unasked was the wrong call, so it was reverted rather than the test
loosened.

**No `CBS_ABI_VERSION` change.** The metadata struct is private to `cbs.h`;
`cbs_public.h` is untouched, checked by diff. The new field is also unrelated to
that macro, which versions this library's own structures, and the manual, the
specification, the integration contract and the embedding guide each say so in
one sentence, because the same word caused a real mistake on 2026-10-31.

**The legacy `cbs_build_package` entry point carries it too.** It writes its
manifest through a different function from the pipeline, and an embedder using
it would otherwise get an artifact disagreeing with the CLI's about one recipe.
A test in `package-test.c` covers it, and removing the line makes that test
fail.

**One decision left as it was:** the manifest writer still writes any value it
is handed. #313 records why, and `abi` is protected by validation and not by the
writer.

## A defect found first, and fixed apart (#313)

Scoping this showed that an `output`'s own `license`, `requires` and `replaces`
were never validated, and that a license containing a newline injected a line
into the artifact's manifest. `abi` joins that item set and is keyed on by cixd,
so it was fixed first, as its own ticket and its own commit.

## Acceptance criteria

- `parser-validation.sh`: eight new fixtures, 88 cases in all. Valid: `abi` at
  package and output level. Invalid, each with an `.expect`: empty, whitespace,
  a newline, a duplicate, a duplicate in an output, an empty one in an output,
  and one declared after a phase. The valid one fails against the v0.1.128
  engine.
- `multi-output-test.sh`, on the shared fixture, where the package declares 17,
  `cixctl` declares 2 and `cix-tools` declares none: each artifact carries its
  own `metadata abi` and nowhere else; `cix-tools` has none and does not report
  17; and `explain` reports 17, 2 and null on the three entries.
- `cli-contract-test.sh`: `"abi":"7"` at the top level and on the main entry,
  and `"abi":null` for a recipe that declares none.
- `package-test.c`: the legacy entry point records `metadata abi 17`.
- The gates behaved as designed. Adding the keyword made the parser-to-spec
  check fail alone, because the manual check is driven by the spec's list and
  would have been blind to it. Regenerating section 2.4's table was verified
  first: the layout algorithm reproduces the existing table byte for byte. Then
  the manual check failed until the manual showed `abi`.
- The manual's new example is tagged `cbs check`, and **it validated but did not
  build**: `write` does not create parent directories, which the manual's first
  recipe handles with a `mkdir`. Validating is not building, and this example
  has no sources, so it was built by hand; it now builds, verifies and reports
  `abi 17`.

Full gate green; all 2138 corpus definitions validate at `origin/main`
`3cb1b15`. The one recipe containing the bare word, `longwire@0.5.39-2`, uses it
in a changelog string, and validates before and after.

## Not done, on purpose

No consumer-side recording ("built against hibr 16") and no comparison: that is
cixd's, and the ticket says so. No `abi` in `inspect`. No second tag for a
buildable example.
