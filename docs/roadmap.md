# CBS delivery roadmap

Release baseline: **v0.1.62**. The complete standalone implementation and its
regression gate are shipped and tagged. The repository has no open issue queue
at this baseline; the workstreams below describe ownership boundaries and
future qualification, not unclaimed defects in the current release.

This roadmap maps the repository's historical issue records to the current
implementation. Issues are grouped by applicability, not original issue
number. Completed records remain useful validation evidence. New work should be
opened explicitly in the issue tracker with an acceptance test and a release
target.

## Complete foundation

Issues #1–#22, #86–#107, and #112–#114 are implemented in the current tree.
They cover CPDL grammar and validation, direct execution, filesystem and source
operations, source fetching, workspace preparation, deterministic phase plans,
standalone builds, and the end-to-end user workflow.

## Workstream 1: artifact correctness

Applicable issues: #23–#26, #54–#56, #69, #95, #100, #107, #108, #115.

Status: the production tree writer, verifier, extractor, deterministic typed
manifest, v2 CIXPKG format, and hostile-input corpus are implemented. CIXPKG v1
is intentionally unreadable; signatures remain an external repository concern.
See [the integration blocker register](integration-blockers.md).

## Workstream 2: usable CLI

Applicable issues: #20, #78, #97, #110.

Status: structured diagnostics, phase-aware failure reporting, explain output,
bounded job policy, child resource limits, and interrupt cleanup are
implemented. `--events jsonl`, `--prune-policy`, `--finalize-command`, and
`--firmware-root` are implemented and documented in the user manual. As of
2026-09-18 every extraction and assertion failure names the member or path and
the rule that rejected it. Additional machine-readable operation modes remain
optional CLI work.

## Workstream 3: dependencies and trust

Applicable issues: #27, #42, #45, #61, #72, #109.

Status: dependency declarations, role-aware phase inputs, cycle checks, source
digests, package identity, and ELF `DT_NEEDED` observation are implemented.
Graph resolution, lock files, conflict handling, and offline graph replay
remain outside standalone CBS; package signatures and key management are
cixd/repository-owned.

## Workstream 4: qualification

Applicable issues: #64, #65, #77, #79, #81, #106.

Hostile archive/package fixtures, mutation coverage, cancellation, resource
limits, and reproducibility gates are implemented. Clean-host CI remains a
qualification task.

## Workstream 5: production integration

Applicable issues: #35–#44 and #57–#85.

These require inputs not present in this repository: a versioned cixd protocol,
production sandbox and repository services, and the first authoritative CPDL
recipe corpus. Legacy shell recipes are the replacement target, not a CBS
runtime dependency. See [the integration blocker register](integration-blockers.md).

## Language completeness (2026-09-18)

The CPDL gaps the recipe-corpus audit recorded are closed: `each` iteration
(ADR-0035), `replace ... until`, `stage library` (ADR-0036), `require
symlink`, and portable-case environment names. What remains for the corpus is
GCC's source layout and toolchain model, and declaring the executor
capabilities a build image must provide.

## Recommended order

1. Harden CIXPKG and refresh stale validation records.
2. Improve CLI diagnostics and machine-readable operation modes.
3. Add dependency, provenance, and signature foundations.
4. Add security, reproducibility, resource, and CI qualification.
5. Integrate cixd and migrate the authoritative recipe corpus.
