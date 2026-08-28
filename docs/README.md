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
