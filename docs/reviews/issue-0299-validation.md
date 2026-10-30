# Issue #299 validation: one recipe, several packages

Implements ADR-0038, accepted 2026-10-30.

## What ships

```cpdl
package "cix" {
    version "1.4.0"
    release 2
    format "cixpkg"
    requires { runtime { library "libarchive" } }

    output "cixctl" {
        files { "usr/bin/cixctl" }
        requires { runtime { library "libcurl" } }
        license "LGPL-3.0-or-later"
    }
    output "cix-tools" { files { glob "usr/bin/mk*" } }

    build { ... }
}
```

```text
cbs build cix.cbs --arch x86_64 --staged WS --output-dir DIR
  -> cixctl-1.4.0-2-x86_64.cixpkg      usr/bin/cixctl
  -> cix-tools-1.4.0-2-x86_64.cixpkg   usr/bin/mkbootroot, usr/bin/mkinstalleriso
  -> cix-1.4.0-2-x86_64.cixpkg         usr/bin/cixd, and the directories
```

One source pinned once, one build, one fingerprint shared by all three. The
CIXPKG format is unchanged: each output is an ordinary v2 artifact.

## Decisions carried out of the ADR

Each follows from the owner's principle — one source of truth, no parallels —
and the ADR records why. In the implementation:

- **The partition is one walk, then a split** (`cbs_manifest_write_outputs`).
  Walking `$dest` once per output would let two manifests disagree about
  ordering or a digest, which is the reasoning ADR-0037 applied to the payload
  index.
- **A path two outputs claim is refused**, naming the path and both outputs,
  rather than resolved by declaration order. Silent precedence would make a
  package's contents depend on the order its outputs happen to appear in.
- **A path no output claims belongs to the main package**, so a recipe carving
  one binary out of a large package does not have to enumerate the rest.
- **All outputs or none.** A failure part way through unlinks what it wrote; a
  half-written set would leave a repository holding artifacts for a build that
  failed.
- **`--output-dir` with canonical filenames**, reusing
  `cbs_identity_artifact_filename()`. `--output FILE` is refused for a
  multi-output recipe and is documented as the legacy single-output form, not a
  peer: a second spelling for one thing is the parallel the principle forbids.
- **The report summarises; the stream enumerates.** `output_count` is new, and
  `artifact`/`artifact_digest` are left empty when it exceeds 1, because one
  path cannot stand for N without looking complete. Totals accumulate, since a
  total is one fact about the build.
- **`explain --json` always reports `outputs`**, main first, even for a
  single-output recipe, so a consumer reads one shape.

`CBS_ABI_VERSION` is 6: `CbsBuildReport` gained `output_count`.

## Two things the ADR did not foresee

**The main output's `requires`.** `explain --json` reported `requires: null`
for the main package while the top-level `requires` held the package's
dependencies, and the two used different serializations in one document --
`{role:{kind:[names]}}` at the top level, `{role:[{kind,name}]}` per output.
A consumer iterating `outputs`, which 4 invites, would read the main package as
having none. 2 says the package declaration *is* the main output, so its own
`requires` is the package's; one serializer now emits both, and an output that
declares none reports `{}` rather than a null. Duplication in a projection is
not a second source of truth -- the recipe is the source -- but two shapes for
one concept is exactly the parallel the principle forbids.

**Directories.** A selector naming `usr/bin/cixctl` does not claim `usr` or
`usr/bin`, so a recipe whose outputs claim every *file* still leaves the
directories for the main package. That is correct, and it means §2's "no files"
is implemented as **no entries**: a directory entry is the only record of a
non-default directory mode, and omitting the main package in that case would
lose it silently — the failure class #285 and #294 were about. A recipe meaning
to emit only its named outputs claims the directories too, which
`files { glob "**" }` does, and main is then omitted as §2 intends. Recorded as
an implementation note on the ADR rather than by editing the decision, per
ADR-0029.

Verified: directory modes survive the partition (`0700` and `0600` preserved),
and a carved output extracts standalone into an empty root because CIXPKG
creates implicit parents — so directory entries are **not** duplicated into
every output that needs them, which would have been a parallel.

**Two bugs worth recording**, both found by running the thing rather than
reading it:

1. The generic duplicate-declaration check rejected **two outputs outright**.
   A one-output test passes, so this was invisible until a second output
   existed. `output` now joins `capability` and `privileged` as repeatable, and
   duplicate *names* are caught where names are checked.
2. Passing `&outputs[0].spec` to the partition **segfaulted**: `CbsOutputSpec`
   is the first member of the pipeline's own struct, so the array strided by
   the wrong size and read past the first element. The pipeline now builds a
   contiguous `CbsOutputSpec` array. A type-punned array that happens to work
   for one element is exactly the shape that fails on two.

## Acceptance criteria

`tests/multi-output-test.sh`, wired into the gate, asserts on the shipped
fixture:

1. three artifacts with canonical names, from one build;
2. the partition — each claimed path in exactly one artifact, and neither
   `cixd` in a carved output nor `cixctl` in the remainder;
3. a per-output `license` in that output's manifest **and nowhere else**, and
   each output's `requires` in `explain --json` and no other's, in the same
   shape the top-level `requires` uses;
4. every artifact verifies alone, and a carved one extracts standalone;
5. one `artifact-finalized` per output, `output_count` 3, and an empty
   `artifact` field in the report;
6. the same provenance in every output -- one recipe digest, one engine, one
   target, one `build_fingerprint` line -- with only the per-output license and
   the manifest digest differing, and three distinct manifest digests, so the
   partition really split the tree;
7. **determinism**: a second build is byte-identical per output;
8. `--output` refused for a multi-output recipe, with no artifact written;
9. overlapping outputs refused naming both, with **no artifact left behind**.

Validation rejections carry `.expect` files: duplicate output name, an output
claiming nothing, and a pattern without `glob`. The parser suite went from 68
to 71 cases.

Verified by stashing only `src/` and running the new test against the
pre-feature engine, where it fails.

All 1969 corpus recipes still validate: the grammar is additive and no existing
recipe declares an output.

## Not in scope

cixd's own changes to install and resolve an output by name, tracked in cix.
The approval shape in ADR-0038 §3 likewise remains the repository's decision;
what ships here is what makes any shape checkable.
