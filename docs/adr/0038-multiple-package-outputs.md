# ADR-0038: Several packages from one recipe

- Status: **Accepted**, 2026-10-30, by the owner.
- Date: 2026-10-30
- Issue: cix-build-system#299, for cix#378

## Context

A recipe builds once and emits exactly one CIXPKG. Its identity is
`(name, version, release, architecture)`, derived from the package declaration
and `--arch`, and the artifact is the whole of `$dest`.

cix#378 needs `cixctl` without the rest of the `cix` package — not `cixd`,
`cix-init`, `mkbootroot`, `cix-install`, `cix-recover`, `mkinstalleriso` or
`cix-boot.efi`. Today the only way to get that is a second recipe pinning the
same `cix` release tarball and built beside the first every release. The owner
rejected exactly that shape for tcc's win64 target in cix ADR-0363: one source
pinned twice drifts the first time a version bump misses one copy.

This is what Debian calls one source package producing several binary packages.

Measured on 2026-10-30: no recipe in the corpus produces more than one package,
and no earlier issue here asks for it. The user manual describes one package per
recipe throughout.

## Decision

### 1. Syntax: a repeatable `output`, with the package as the main one

```cpdl
package "cix" {
    version "1.4.0"
    release 2
    format "cixpkg"

    requires { runtime { library "libarchive" } }

    output "cixctl" {
        files { "usr/bin/cixctl" }
        requires { runtime { library "libcurl" } }
    }

    build { ... }
}
```

`output` is a package item, repeatable, carrying `files` selectors and
optionally its own `requires`, `license` and `replaces`. The `package`
declaration remains an output — the **main** one — and receives every staged
path no `output` claimed.

Chosen over naming every output explicitly, which is more uniform. The corpus
holds 1969 definitions and none declares an output; an implicit main keeps every
one of them valid and unchanged, and makes the feature cost nothing to recipes
that do not want it. A uniform scheme would require editing all 1969 to say
what they already say by default.

`files` reuses the glob vocabulary `copy` and `require glob` already have, so
there is no third selector syntax to specify or test.

### 2. Ownership: no overlap, no exact partition required

- A staged path claimed by **two** outputs is a build failure, reported as
  `CPDL-E4007` naming the path and both outputs. Silent precedence between
  outputs would make a package's contents depend on declaration order.
- A path claimed by **no** output goes to the main package. Requiring an exact
  partition would force every recipe to enumerate its whole tree, which is the
  opposite of what the ticket asks for and would make `output` unusable for
  carving one binary out of a large package.
- A declared `output` that claims **nothing** is a build failure. An empty
  package is never intended; it means a selector is wrong, and failing names
  which.

- When a recipe declares at least one `output` and those outputs claim every
  staged path, the main package has no files and **is not emitted**. The recipe
  produces exactly the outputs it named.

That last rule exists because the second thing anyone will want is not the
ticket's shape. `cixctl` carved out of `cix` leaves a large main package, but a
`-bin`/`-dev`/`-doc` triple from one tarball leaves nothing over, and that
recipe has to be writable. Refusing an empty main would block it until a "no
main output" marker existed; emitting an empty artifact would ship a package
with no files. Omitting it needs no new keyword and makes both shapes
expressible.

It is safe against a mis-written selector because the omission is loud rather
than silent: an over-claiming selector means the main artifact the author
expected is simply absent, and `cbs explain` lists a recipe's outputs before
anything is built.

Scoped deliberately to recipes that declare an output. A recipe with no
`output` and an empty `$dest` emits an empty artifact today, and that does not
change.

### 3. Identity: per output; one build action, one fingerprint

Each output is its own `(name, version, release, architecture)`. The output's
name replaces the package name; `version`, `release` and the `--arch` target are
shared, because they describe the source and the build, not the file set.

Each output is an ordinary CIXPKG v2 artifact with its own manifest and its own
`manifest-digest`. **The CIXPKG format does not change.**

Every artifact records the same `build_fingerprint`, because the fingerprint
covers the build *action* — recipe, sources, architecture, command and library
roots, tool identities — and there is one action. That is a feature: the shared
fingerprint is what says these artifacts came from one build and cannot have
drifted from each other.

**For the owner, and the one place this reaches outside CBS:** an approval today
names one artifact digest. With N outputs there are N digests from one action.
CBS can make that discoverable — `explain` reports the output names before a
build, and an `artifact-finalized` event carries each digest during one — but
whether an approval names N digests, or the fingerprint plus N, is a repository
and cixd decision, tracked in cix rather than here.

### 4. `cbs explain --json`: an `outputs` array, always

`explain` gains `outputs`: an ordered array, main package first, each with its
name, its `files` selectors and its own `requires`. It is present even for a
single-output recipe, as a one-element array naming the main package, so a
consumer has one shape to read rather than two.

This follows the precedent set for `upstream`, which the integration contract
made "always an object when declared … so cixd has one stable shape". cixd's
package resolution reads `explain`, and it is the only way an orchestrator can
know what a recipe provides without building it.

One difference from that precedent: `upstream`'s shape was settled for a field
cixd had asked for, whereas `outputs` arrives unasked. Adding a top-level key is
additive but it is still a change to what every existing `explain` consumer
reads, so cixd should tolerate an unknown key before this ships rather than
after.

### 5. CLI: `--output-dir` is the way; `--output FILE` is legacy

`--output FILE` names one path and cannot name N. It becomes a usage error —
exit 2 — when the recipe declares more than one output, rather than silently
writing one of them.

It is kept only because the callers of 1969 recipes use it today. It is **not**
a peer of `--output-dir`: a second spelling for one thing is a parallel, and it
is the parallel that creates the compatibility cliff below. New callers and new
recipes use `--output-dir`, and the manual should present that as the way with
`--output` documented as the legacy single-output form.

Repeated `--output name=FILE` is rejected for the same reason: it would let a
caller name an artifact differently from `cbs_identity_artifact_filename()`,
which is two names for one artifact.

`--output-dir DIR` writes each artifact as
`name-version-release-arch.cixpkg`. That filename already exists as
`cbs_identity_artifact_filename()` and is already the canonical spelling, so
nothing new is specified.

**This is the design's one backward-compatibility cost, and it is worth
naming.** A recipe can gain its second output in a later revision, and at that
moment every caller invoking it with `--output FILE` starts failing with no
change to their own invocation. A recipe adding an output is therefore a
CLI-visible change for everyone who builds it — the same class of silent
coupling the ticket's own reasoning objects to, so it should be a deliberate
choice rather than a discovered one.

### 6. Report and events: per-artifact data stays in the stream

`CbsBuildReport` has one `artifact_path[4096]`, one `artifact_digest[65]` and
one `artifact_bytes`. An unbounded per-output array does not belong in a C
struct that embedders allocate.

So: one `artifact-finalized` event per output, each carrying that output's
identity, path, digest and size — the stream is already the per-item channel,
and #297 and #298 have just made it dependable.

The report then states one thing and does not imply another. Its `fingerprint`
is the single key for the build, which it already carries. Its `artifact_path`
and `artifact_digest` are populated **only** when the recipe has exactly one
output, and are empty otherwise, alongside a new `output_count`.

Keeping those populated with the main package while the stream carried all N
would be two representations of one build's artifacts, where the report's looks
complete and is not — a consumer reading only the report would take a partial
truth for the whole and could not detect it. Empty-and-counted is honest: it
says this shape is not representable here, read the stream. Totals — bytes,
counts — still cover every output, because a total is one fact about the
build.

**The limit of that claim.** Whether anything depends on the report's
`artifact_digest` being *the* package's identity could not be checked from this
machine: no cixd checkout is present, and nothing in the reachable repositories
references `cbs.build-report`, `artifact_digest` or `--report` at all. So this
section rests on its shape, not on evidence. If something in cixd reads that
digest as the package identity, §6 is wrong and the report needs a per-output
representation after all.

This still changes `CbsBuildEvent`'s meaning for one event type and adds
report totals, so it takes a `CBS_ABI_VERSION` bump — the **third** of this
run, after 3 → 4 for #298 and 4 → 5 for #297, making 6. An embedder linking
`libcbs.a` rebuilds three times across these releases, which is worth knowing
before accepting this one.

### 7. Determinism

Each output must be byte-reproducible independently. The reproducibility gate
builds a recipe twice and compares artifacts; with several outputs it compares
all of them, and a multi-output fixture belongs in that gate rather than only in
the parser fixtures.

## Consequences

Costly, and worth being explicit about:

- `src/manifest.c` gains a selector pass: collect once, then partition, rather
  than walking `$dest` once per output. One walk keeps ordering and digests
  consistent between outputs, the same reasoning ADR-0037 applied to the
  payload index.
- `src/package.c`'s pipeline currently writes one manifest and one artifact
  inside one `if`. It becomes a loop over outputs, and the failure path must not
  leave some artifacts written and others not — either all outputs are written
  or none is.
- `cbs explain`, the user manual, the integration contract and the CPDL
  specification all describe one package per recipe and all change.
- `cbs package ROOT` packages a caller-assembled tree and has no recipe. It
  keeps its single-artifact contract; `output` is a recipe concept.

What does not change: the CIXPKG format, phase execution, confinement, the
source model, and every recipe that does not declare an output.

## Alternatives rejected

**A second recipe pinning the same source.** What cix ADR-0363 rejected for
tcc's win64 target, for the reason given there: two pins of one source drift the
first time a bump misses one. It is also what cix#378 is waiting to avoid.

**A post-build splitter outside CBS.** A tool that reads one CIXPKG and emits
several would keep CBS simple, but the outputs would have no build fingerprint
of their own, no recipe-declared `requires`, and nothing in `explain` to tell an
orchestrator they exist. It moves package identity outside the engine that owns
it.

**Per-output phases.** Letting each output run its own `install` would make
outputs independent builds and defeat the point: one build, one source, one
fingerprint.

## The four decisions, under one source of truth

Each had several defensible answers until the governing principle was applied:
no fact may live in two places, because two places drift. That settles all
four, because every rejected alternative creates a second home for something
CBS already knows.

**1. A main package with no files is omitted.** The recipe declares what it
produces. An empty artifact would be a package present in the repository
corresponding to nothing the recipe said — a second, fictional truth. Refusing
it is the third option and blocks the `-bin`/`-dev`/`-doc` shape, so it is not
a candidate.

**2. CPDL 1.0, additive.** A 1.1 means two specification documents, two
conformance claims, and recipes valid against one and not the other: a parallel
language. §1 reserves a new specification for an *incompatible* revision, and
this is compatible — all 1969 corpus recipes stay valid and mean exactly what
they mean today. `tools`, `resources` and `privileged` were added this way.

**3. An approval names the fingerprint; the digests derive from it.** The
sharpest case. N approval lines for N digests are N facts that can drift from
each other and from the build — the two-pins-of-one-source failure cix ADR-0363
rejected, in another costume. The build fingerprint is the single identity of
the action: each artifact already records it and carries its own digest, so an
orchestrator approves the fingerprint and checks that every artifact's recorded
fingerprint matches before trusting its digest. One approved fact, N derived
facts verified against it.

The decision remains the repository's and is tracked in cix. CBS's obligation
is to make it checkable, which it already is: the fingerprint is in every
artifact's metadata, and the digests are in `explain` and the event stream.

**4. `--output-dir`, with canonical filenames.** The canonical filename is the
one source of truth for what an artifact is called, and `--output-dir` is the
only spelling that keeps it so. See §5, which also makes `--output FILE` the
legacy form rather than an equal alternative.

## Implementation note (v0.1.119)

Implementing §2 exposed a case the decision did not resolve: directories.

A staged directory is a manifest entry because it carries a mode. A selector
naming `usr/bin/cixctl` does not claim `usr` or `usr/bin`, so a recipe whose
outputs claim every *file* still leaves the directories unclaimed, and the main
package is emitted holding them.

That is the right outcome, so "the main package has no files" in §2 is
implemented as **no entries at all**. An artifact holding only directories is
not empty: those entries are the only record of a non-default directory mode,
and omitting the main package in that case would lose them silently — the
failure class #285 and #294 were about. A recipe that genuinely means to emit
only its named outputs claims the directories too, which `files { glob "**" }`
does, and the main package is then omitted as §2 intends.

Verified: directory modes survive the partition (`0700` and `0600` preserved
through a claimed subtree), and a split output extracts standalone into an
empty root because CIXPKG creates implicit parents — so directory entries are
not duplicated into every output that needs them.

This records what the accepted decision means in a case it did not foresee. It
does not change it; per ADR-0029 a changed decision would need a superseding
record.

## Accepted as written

Accepted on 2026-10-30 with all four decisions as recommended, under the
owner's stated principle: one source of truth, no parallels.

The approval shape in §3 remains the repository's to decide and is tracked in
cix. What is accepted here is CBS's obligation — the fingerprint in every
artifact's metadata, the digests in `explain` and the event stream — which is
what makes any approval shape checkable.
