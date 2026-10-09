# Architecture decision records

An ADR records a decision, its rationale, its consequences, and any question
left deliberately undecided. Once accepted, an ADR is not rewritten to hide a
changed decision; a later ADR supersedes it, and ADR-0029 states that rule.

This index is complete. All 37 records, in order:

- [ADR-0001](0001-cbs-and-cpdl.md) — Establish CBS and CPDL
- [ADR-0002](0002-cbs-bootstrap-and-libraries.md) — Bootstrap CBS and permit linked base libraries
- [ADR-0003](0003-cbs-file-extension.md) — Use `.cbs` for package definition files
- [ADR-0004](0004-native-helper-governance.md) — Governance for package-specific native helpers
- [ADR-0005](0005-source-networking-boundary.md) — Source networking boundary
- [ADR-0006](0006-archive-extraction-formats.md) — CBS v1 archive extraction formats
- [ADR-0007](0007-build-sandbox-and-dependency-observation.md) — CBS v1 sandbox and dependency observation
- [ADR-0008](0008-cbs-command-surface.md) — CBS command surface
- [ADR-0009](0009-third-party-source-policy.md) — Third-party source policy
- [ADR-0010](0010-repository-and-installation-policy.md) — Repository metadata and installation policy
- [ADR-0011](0011-stage0-library-admissions.md) — Stage-0 trusted library admissions
- [ADR-0012](0012-stage-zero-entry-point.md) — Stage-zero build entry point
- [ADR-0013](0013-seed-provenance.md) — Seed provenance metadata
- [ADR-0014](0014-seed-transition.md) — Seed-to-CBS-managed transition
- [ADR-0015](0015-cixd-boundary.md) — CBS and cixd boundary
- [ADR-0016](0016-sandbox-owner.md) — Sandbox ownership
- [ADR-0017](0017-image-model.md) — Cix image composition
- [ADR-0018](0018-container-recipes.md) — Container recipes and CPDL
- [ADR-0019](0019-image-recipes.md) — Image recipes and CPDL
- [ADR-0020](0020-immutable-install.md) — CBS install and immutable images
- [ADR-0021](0021-artifact-repository.md) — Unified artifact repository
- [ADR-0022](0022-build-observability.md) — Build observability
- [ADR-0023](0023-host-builds.md) — Host build future
- [ADR-0024](0024-build-provenance.md) — Build provenance mandate
- [ADR-0025](0025-userns-ownership.md) — User namespace ownership
- [ADR-0026](0026-structural-build-dependencies.md) — Structural build dependencies
- [ADR-0027](0027-shell-retirement.md) — Shell build retirement
- [ADR-0028](0028-legacy-artifacts.md) — Legacy shell artifacts
- [ADR-0029](0029-adr-supersession.md) — ADR supersession and terminology
- [ADR-0030](0030-release-default.md) — Release default and identity normalization
- [ADR-0031](0031-replacement-contract.md) — Production pipeline replacement contract
- [ADR-0032](0032-cixpkg-detached-signatures.md) — Use detached signatures for CIXPKG artifacts
- [ADR-0033](0033-revision-selected-artifact-format.md) — Select artifact format from the immutable recipe revision
- [ADR-0034](0034-bounded-run-stdout.md) — Bounded CPDL command stdout
- [ADR-0035](0035-parse-time-iteration.md) — Parse-time list iteration
- [ADR-0036](0036-stage-sandbox-libraries.md) — Staging shared libraries from the build sandbox
- [ADR-0037](0037-cixpkg-payload-index.md) — CIXPKG staged-payload indexing

The decisions a newcomer needs first are ADR-0001 (why CBS and CPDL exist),
ADR-0008 (the command surface and its exit statuses), and ADR-0015 (where CBS
ends and cixd begins).
