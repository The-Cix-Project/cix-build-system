# CBS integration blocker register

This was the hand-off list for moving CBS from a working standalone builder to a
production component, and it is kept as the record of what CBS needed from its
surroundings and who owned each part. A blocker here meant an integration input
or an explicit product decision, not a defect in the standalone path. It is a
planning document, not the repository's active issue queue.

Baseline: CBS v0.1.127. The standalone suite and the process-level integration
contract are shipped. Items below that concern cixd's own environment are
cixd's to document and not this repository's: a claim about cixd's production
state written here goes stale on the cix repository's release cadence, and
nothing in this repository can see that happen. For how cixd embeds CBS, read
that repository, notably its ADR-0199, ADR-0209, ADR-0279, ADR-0305, ADR-0307,
ADR-0360 and ADR-0367.

## Current state

The standalone path is operational:

```text
.cbs recipe -> validate -> fetch/checksum -> prepare -> execute -> manifest
            -> CIXPKG -> verify -> extract
```

`make test` passes, including the end-to-end HTTP fetch, cache, package
verification, extraction, and mode-preservation tests, and it is fully offline.
The real-package smoke test is part of the gate but needs a caller-supplied
`CBS_UPSTREAM_CACHE` directory of verified source files: with that unset it
prints `SKIP` and exits 0, and it never downloads. `make upstream-test` runs
the zstd qualification alone, and `make qualification-test` runs the complete
current gate.

## Blockers and unblock actions

### 1. CIXPKG contract: production format complete

The tree writer/verifier is used by real builds and matches the normative CIXPKG
v2 specification. The old manifest-only API and v1 reader are retired.

No standalone unblock action remains. Keep the tree format as the sole
production format and treat CIXPKG v2 as the supported reader/writer contract.

### 2. The recipe corpus is converted

Measured on 2026-10-09, the Cix recipe corpus held 1010 CPDL definitions
across 181 distinct packages and **no remaining shell recipes**; 1567 `.sh`
package files exist in its history and none survive. It has grown since: 2016
definitions across 246 packages at `origin/main` `802e05c` on 2026-10-31, all
validating, with no recipe declaring `tools` and three versions of one package,
`cix`, declaring an `output`. The conversion this register was written to track
is therefore done, and the language gaps the corpus audit identified were closed
on 2026-09-18.

That corpus is not in this repository and should not be. It belongs to the
recipe repository; CBS ships only the build-tested `zstd` qualification recipe,
which exists to prove the engine against a real upstream rather than to be a
corpus. Legacy shell recipes are neither a CBS dependency nor CPDL input.

No unblock action remains. The standing requirement is that the qualification
recipe stays current and that a CPDL change is measured against the real corpus
before release, which `cbs validate` over the recipe repository does cheaply.

### 3. cixd integration contract defined

CBS and cixd now have a concrete first-slice contract. cixd owns discovery,
dependency/image composition, cache population, container creation, signing,
publication, and transactions. CBS owns CPDL validation, phase execution,
manifest/CIXPKG creation, verification, and the finalization and phase-event
callbacks.

The contract is documented in [CBS and cixd integration contract](integration-contract.md).
An HTTP/OpenAPI adapter is intentionally deferred: the first useful path is a
parent process invoking CBS inside its already-created build container.

### 4. Production qualification is split by who owns the environment

This repository qualifies parser, archive, package, reproducibility, and
resource-limit behavior locally, by hand, with `make test`; that is the boundary
it can honestly claim. The sandbox, the artifact and cache service, the
signing-key policy, and the target architecture and toolchain matrix belong to
cixd and the repository service, and are documented in the cix repository.

A clean-host CI image was assessed as useful qualification and **declined by the
owner on 2026-10-09**; the gate is run on a checkout for each release. No
unblock action is open in this repository.

## External inputs

None is outstanding for CBS. The input this section once asked for, a reachable
cixd endpoint to qualify against, is no longer open, and the cix repository
documents the integration. The recipe-corpus inputs were answered earlier
(blocker 2).

Nothing is needed to use CBS standalone.
