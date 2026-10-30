# Cix Build System documentation

CBS is the Cix Build System, **CPDL** is the Cix Package Definition Language it
reads, and **CIXPKG** is the artifact format it produces and verifies. This
directory holds the language and artifact specifications, the practical guides,
and the project's decision and validation history.

Current release: **CBS v0.1.123**. `VERSION`, the release tag, and
`cbs --version` are kept in sync by the release gate.

## Start here

| If you want to | Read |
| --- | --- |
| Decide whether CBS fits at all | [scope and determinism contract](standalone-scope.md) |
| Write your first recipe | [user manual](user-manual.md) |
| Operate or script CBS | [standalone runbook](standalone-runbook.md) |
| Know exactly what CPDL accepts | [CPDL 1.0 specification](spec/cpdl-1.0.md) |
| Drive CBS from a parent process | [cixd embedding guide](guides/cixd-embedding.md) |
| Link `libcbs.a` into a program | [library boundary guide](library.md) |
| Know what CBS owns and what cixd owns | [repository status](repository-status.md) |
| Cut a release | [CI and release guide](guides/ci-and-release.md) |

Guides and specifications are deliberately separate: a guide explains a
workflow and may change freely, while a specification defines the accepted
language, the artifact format, and the machine-readable contracts, and changes
only by decision.

## Specifications

Normative. These define what a conforming implementation must accept, produce,
and refuse.

- [CPDL 1.0 language specification](spec/cpdl-1.0.md) — lexical grammar,
  document grammar, phases and operations, the validation contract, the
  diagnostic contract, and process exit statuses
- [CIXPKG v2 binary specification](spec/cixpkg-1.0.md) — header layout,
  manifest grammar, and the reader/writer rules

## Guides

Practical, non-normative.

- [CBS user manual](user-manual.md) — from a fresh checkout to a verified
  artifact, plus a complete recipe reference and troubleshooting
- [Standalone CBS runbook](standalone-runbook.md) — the operator's and CI
  sequence
- [Scope and determinism contract](standalone-scope.md) — what reaches a build,
  and what cannot
- [CBS and cixd integration contract](integration-contract.md) — the
  process-level boundary cixd builds against
- [cixd embedding guide](guides/cixd-embedding.md) — choosing and using one of
  the two integration shapes
- [Library boundary guide](library.md) — what `libcbs.a` and `cbs/cbs.h`
  promise, and the consumer checklist
- [CI and release guide](guides/ci-and-release.md) — qualifying a checkout,
  bounds instrumentation, and the release checklist

## Project state

- [Repository status and scope](repository-status.md) — the authority on what
  this repository owns and what belongs to cixd
- [CBS delivery roadmap](roadmap.md) — workstreams and ownership boundaries
- [Integration blocker register](integration-blockers.md) — the remaining
  inputs for production integration
- [CPDL and CBS test coverage](cpdl-test-coverage.md) — each surface mapped to
  the test that asserts it
- [Native helper registry](native-helpers.md) — required by ADR-0004, and
  correctly empty
- [Recipe-corpus coverage audit](cpdl-corpus-audit.md) — historical; the record
  of what the shell corpus contained and how CPDL answered each construct

Open work is tracked in the issue tracker, not in these documents.

## History

- [Architecture decision records](adr/README.md) — all 38, with the rule that a
  decision is superseded rather than rewritten
- [Validation records](reviews/README.md) — all 161, one per closed issue,
  describing the implementation at that issue's close date rather than today

## Release notes

- [Release notes](release-notes.md) — for maintainers, recipe authors and
  embedders: what to act on, what was unblocked, and the diagnostics that
  changed. Its heading states the releases it covers; the path never changes,
  so a link to it does not rot

## Licence and marks

CBS is free software under the GNU Lesser General Public License, version 3, or
at your option any later version (`LGPL-3.0-or-later`). The LGPL is a set of
additional permissions on top of the GPL, which is why both texts are in the
repository: [COPYING.LESSER](../COPYING.LESSER) is the LGPLv3 and
[COPYING](../COPYING) the GPLv3 it builds on.

The Cix name and marks are governed separately, by
[TRADEMARK.md](../TRADEMARK.md), which also states what counts as an official
Cix artifact. A licence grants no right to the marks, and neither grants
infrastructure or support.

Nothing in these documents is a licence term. Where a guide or specification
and a licence text disagree, the licence text governs.

The canonical [Cix Build System logo](../brand/cix-cbs-logo.svg) is maintained
in the repository brand directory.
