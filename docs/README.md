# Cix Build System documentation

This directory contains the architecture and language documentation for:

The canonical [Cix Build System logo](../brand/cix-cbs-logo.svg) is maintained
in the repository brand directory.

- **CBS** — the Cix Build System;
- **CPDL** — the Cix Package Definition Language; and
- **CIXPKG** — the distributable package format produced and consumed by CBS.

## Architecture decisions

- [ADR-0001: Establish CBS and CPDL](adr/0001-cbs-and-cpdl.md)
- [ADR-0002: Bootstrap CBS and permit linked base libraries](adr/0002-cbs-bootstrap-and-libraries.md)
- [ADR-0003: Use `.cbs` for package definition files](adr/0003-cbs-file-extension.md)

ADRs record decisions, their rationale, their consequences, and any questions
that remain deliberately undecided. Once accepted, an ADR is not rewritten to
hide a changed decision; a later ADR supersedes it.

## Specifications

- [CPDL 0.1 language specification](spec/cpdl-0.1.md)

## Validation records

- [Issue #1: CPDL 0.1 grammar and diagnostics](reviews/issue-0001-validation.md)
- [Issue #2: CPDL lexer, parser, AST, and validator](reviews/issue-0002-validation.md)
- [Issue #3: Direct `run` execution](reviews/issue-0003-validation.md)
- [Issue #4: CPDL filesystem vocabulary](reviews/issue-0004-validation.md)
- [Issue #5: Source edits and assertions](reviews/issue-0005-validation.md)
- [Issue #6: Failure orchestration](reviews/issue-0006-validation.md)
- [Issue #7: Canonical package identity](reviews/issue-0007-validation.md)
- [Issue #8: Named sources and checksum verification](reviews/issue-0008-validation.md)
- [Issue #9: Dependency roles and kinds](reviews/issue-0009-validation.md)
- [ADR-0004: Native helper governance](adr/0004-native-helper-governance.md)
- [Native helper registry](native-helpers.md)
- [ADR-0005: Source networking boundary](adr/0005-source-networking-boundary.md)
- [ADR-0006: CBS v1 archive extraction formats](adr/0006-archive-extraction-formats.md)
- [Issue #12: Verified source fetching](reviews/issue-0012-validation.md)
- [Issue #13: Archive format decision](reviews/issue-0013-validation.md)
- [Issue #14: Confined archive extraction](reviews/issue-0014-validation.md)
- [Issue #15: Digest kinds](reviews/issue-0015-validation.md)
- [ADR-0007: Build sandbox and dependency observation](adr/0007-build-sandbox-and-dependency-observation.md)
- [Issue #16: Sandbox and dependency decision](reviews/issue-0016-validation.md)
- [Issue #17: Jobs ceiling](reviews/issue-0017-validation.md)
- [Issue #18: Staged-tree policy](reviews/issue-0018-validation.md)
- [Issue #19: Manifest ordering](reviews/issue-0019-validation.md)
- [ADR-0008: CBS command surface](adr/0008-cbs-command-surface.md)
- [Issue #20: Command contracts](reviews/issue-0020-validation.md)
- [Issue #21: TCC-only compiler enforcement](reviews/issue-0021-validation.md)
- [ADR-0009: Third-party source policy](adr/0009-third-party-source-policy.md)
- [Issue #22: Third-party source policy](reviews/issue-0022-validation.md)
- [CIXPKG v1 binary specification](spec/cixpkg-1.0.md)
- [Issue #23: CIXPKG v1 layout](reviews/issue-0023-validation.md)
- [Issue #24: CIXPKG creation](reviews/issue-0024-validation.md)
- [Issue #25: CIXPKG inspection and verification](reviews/issue-0025-validation.md)
- [Issue #26: Safe extraction and installation](reviews/issue-0026-validation.md)
- [ADR-0010: Repository and installation policy](adr/0010-repository-and-installation-policy.md)
- [Issue #27: Repository and transaction policy](reviews/issue-0027-validation.md)
- [ADR-0011: Stage-0 library admissions](adr/0011-stage0-library-admissions.md)
- [Issue #28: Stage-0 library admissions](reviews/issue-0028-validation.md)
- [ADR-0012: Stage-zero entry point](adr/0012-stage-zero-entry-point.md)
- [Issue #29: Stage-zero entry point](reviews/issue-0029-validation.md)
- [Issue #30: Canonical cbs.cbs](reviews/issue-0030-validation.md)
- [Issue #31: Stage reproducibility](reviews/issue-0031-validation.md)
- [ADR-0013: Seed provenance](adr/0013-seed-provenance.md)
- [Issue #32: Seed provenance](reviews/issue-0032-validation.md)
- [ADR-0014: Seed transition](adr/0014-seed-transition.md)
- [Issue #33: Seed transition](reviews/issue-0033-validation.md)
- [Issue #34: Dependency-cycle gate](reviews/issue-0034-validation.md)
- [ADR-0015: CBS and cixd boundary](adr/0015-cixd-boundary.md)
- [Issue #35: CBS and cixd boundary](reviews/issue-0035-validation.md)
- [ADR-0016: Sandbox ownership](adr/0016-sandbox-owner.md)
- [Issue #36: Sandbox ownership](reviews/issue-0036-validation.md)
- [ADR-0017: Image model](adr/0017-image-model.md)
- [Issue #37: Image model](reviews/issue-0037-validation.md)
- [Issue #38: Manifest-derived images](reviews/issue-0038-validation.md)
- [ADR-0018: Container recipes and CPDL](adr/0018-container-recipes.md)
- [Issue #39: Container recipe relationship](reviews/issue-0039-validation.md)
- [ADR-0019: Image recipes and CPDL](adr/0019-image-recipes.md)
- [Issue #40: Image recipe relationship](reviews/issue-0040-validation.md)
- [ADR-0020: Immutable install](adr/0020-immutable-install.md)
- [Issue #41: Immutable image install](reviews/issue-0041-validation.md)
- [ADR-0021: Unified artifact repository](adr/0021-artifact-repository.md)
- [Issue #42: Artifact repository model](reviews/issue-0042-validation.md)
- [ADR-0022: Build observability](adr/0022-build-observability.md)
- [Issue #43: Build observability](reviews/issue-0043-validation.md)
- [ADR-0023: Host builds](adr/0023-host-builds.md)
- [Issue #44: Host build policy](reviews/issue-0044-validation.md)
- [ADR-0024: Build provenance mandate](adr/0024-build-provenance.md)
- [Issue #45: Build provenance](reviews/issue-0045-validation.md)
