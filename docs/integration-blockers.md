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

### 1. CIXPKG contract: production format complete

The tree writer/verifier is used by real builds and matches the normative CIXPKG
v2 specification. The old manifest-only API and v1 reader are retired.

Unblock action: keep the tree format as the sole production format, mark the
legacy API deprecated, and retire it with its compatibility tests.

### 2. The replacement recipe corpus is in progress

The legacy shell recipes are the system CBS is replacing; they are not a CBS
dependency and must not be executed or treated as CPDL input. This repository
contains a staged seed set of migrated recipes, including `tcc`, `gcc`, `cix`,
`kernel`, `squashfs-tools`, `wireless-regdb`, `zstd`, and `libarchive`. They
validate as CPDL, but GCC and kernel remain explicitly non-building migrations.

Unblock action: define the first in-repository CPDL seed set and migrate each
recipe deliberately, with validation and end-to-end build tests. The migration
must preserve declared sources, checksums, dependencies, staged paths, and
toolchain requirements without reintroducing shell execution through a side
door.

### 3. cixd integration contract defined

CBS and cixd now have a concrete first-slice contract. cixd owns discovery,
dependency/image composition, cache population, container creation, signing,
publication, and transactions. CBS owns CPDL validation, phase execution,
manifest/CIXPKG creation, verification, and the finalization and phase-event
callbacks.

The contract is documented in [CBS and cixd integration contract](integration-contract.md).
An HTTP/OpenAPI adapter is intentionally deferred: the first useful path is a
parent process invoking CBS inside its already-created build container.

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
