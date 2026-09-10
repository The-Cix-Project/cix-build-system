# CBS integration blocker register

This is the current hand-off list for moving CBS from a working standalone
builder to a production component. A blocker here means an integration input
or an explicit product decision, not that the CPDL parser or standalone build
path is unusable.

## Current state

The standalone path is operational:

```text
.cbs recipe -> validate -> fetch/checksum -> prepare -> execute -> manifest
            -> CIXPKG -> verify -> extract
```

`make test` passes, including the end-to-end HTTP fetch, cache, package
verification, extraction, and mode-preservation tests.

## Blockers and unblock actions

### 1. CIXPKG contract must be made single-source

The new tree writer/verifier is used by real builds, but the old
manifest-only `cbs_cixpkg_write()`/`cbs_cixpkg_verify()` API remains for
compatibility tests. The implementation also stores textual 64-character
SHA-256 values in the section table, while the normative specification
describes binary 32-byte digests and a header digest.

Unblock action: choose one CIXPKG v1 wire layout, update the implementation,
tests, and spec together, then remove or clearly mark the legacy API. This is
an internal CBS decision; no external service is needed.

### 2. The replacement recipe corpus is not yet in this repository

The legacy shell recipes are the system CBS is replacing; they are not a CBS
dependency and must not be executed or treated as CPDL input. This repository
contains CPDL examples and fixtures, but not yet an authoritative production
set of `.cbs` recipes covering the packages CBS is expected to build.

Unblock action: define the first in-repository CPDL seed set and migrate each
recipe deliberately, with validation and end-to-end build tests. The migration
must preserve declared sources, checksums, dependencies, staged paths, and
toolchain requirements without reintroducing shell execution through a side
door.

### 3. cixd integration needs an adapter mapping

CBS already has callback boundaries for daemon requests, fetch, sandboxing,
signatures, and transactions, but no concrete cixd protocol contract or
HTTP/OpenAPI client adapter in this repository.

Unblock action: select the cixd API version and the initial operation set. The
smallest useful slice is recipe upload/list, source/artifact fetch, and package
artifact publish/verify. Then implement a bounded HTTP adapter against the
OpenAPI contract and test it with a local fixture server.

### 4. Production qualification requires environment ownership

The repository can qualify parser, archive, package, reproducibility, and
resource-limit behavior locally. Production cutover additionally needs the
cixd sandbox, artifact/cache service, signing-key policy, clean-host CI image,
and an authoritative target architecture/toolchain matrix.

Unblock action: provide or identify the clean-host CI environment and the
owner of signing, cache, and cixd test endpoints. Until then, local fixture
tests are the correct boundary and production claims should remain pending.

## What you can provide

The highest-value user inputs are:

- provide or nominate the first CPDL recipe seed set for blocker 2;
- choose the migration acceptance criteria in blocker 2;
- approve the CIXPKG v1 wire-layout decision in blocker 1; and
- identify a reachable cixd test endpoint or authorize a fixture-only adapter
  phase.

Nothing is needed to use CBS standalone today. These inputs are only needed
for compatibility and production integration.
