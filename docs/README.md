# Cix Build System documentation

This directory contains the architecture and language documentation for:

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
