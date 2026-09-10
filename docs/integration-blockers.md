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

### 2. Legacy recipe migration needs a deliberate compatibility policy

The available corpus is `/home/osakka/new_project/recipes`: it contains 77
package families, 1,217 versioned package `build.sh` files, image manifests,
and deployment definitions. Package recipes declare fields such as
`pkg_name`, `pkg_version`, `pkg_source`, `pkg_sha256`, and shell functions
`pkg_build`/`pkg_install`.

CBS recipes are CPDL `.cbs` documents with direct, non-shell execution and a
different staged-tree model. Automatic translation is therefore possible only
for a defined subset; blindly converting shell recipes would weaken CBS’s
safety guarantees.

Unblock action: confirm that this corpus is the migration source of truth and
choose one policy:

1. translate supported recipes to CPDL and report unsupported shell constructs;
2. keep legacy recipes behind a separate compatibility runner; or
3. make CPDL the new source of truth and migrate only a selected seed set.

The first useful deliverable after that decision is a read-only inventory and
translation report, not an automatic publish.

### 3. cixd integration needs an adapter mapping

The adjacent project provides an OpenAPI contract at
`/home/osakka/new_project/docs/api/openapi.yaml`, including package recipe
publication, recipe sync, package installation, artifact cache, and hostbuild
operations. CBS already has callback boundaries for daemon requests, fetch,
sandboxing, signatures, and transactions, but no HTTP/OpenAPI client adapter.

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

- confirm whether `/home/osakka/new_project/recipes` is the authoritative
  corpus;
- choose the migration policy in blocker 2;
- approve the CIXPKG v1 wire-layout decision in blocker 1; and
- identify a reachable cixd test endpoint or authorize a fixture-only adapter
  phase.

Nothing is needed to use CBS standalone today. These inputs are only needed
for compatibility and production integration.

