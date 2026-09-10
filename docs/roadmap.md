# CBS delivery roadmap

This roadmap maps the repository's historical issue records to the current
implementation. Issues are grouped by applicability, not original issue
number. Completed records remain useful validation evidence; open records are
the active delivery queue.

## Complete foundation

Issues #1–#22, #86–#107, and #112–#114 are implemented in the current tree.
They cover CPDL grammar and validation, direct execution, filesystem and source
operations, source fetching, workspace preparation, deterministic phase plans,
standalone builds, and the end-to-end user workflow.

## Workstream 1: artifact correctness

Applicable issues: #23–#26, #54–#56, #69, #95, #100, #107, #108, #115.

Status: the production tree writer, verifier, extractor, deterministic
manifest, and corruption tests are implemented. A legacy manifest-only API is
retained only for compatibility tests and is not part of the production path.
Remaining work is compatibility API retirement and broader hostile-input
coverage. See [the integration blocker register](integration-blockers.md).

## Workstream 2: usable CLI

Applicable issues: #20, #78, #97, #110.

Status: structured diagnostics, phase-aware failure reporting, explain output,
and bounded job policy are implemented. Remaining work is CLI option parsing
ergonomics and additional machine-readable operation modes.

## Workstream 3: dependencies and trust

Applicable issues: #27, #42, #45, #61, #72, #109.

Status: dependency declarations, role-aware phase inputs, cycle checks, source
digests, and package identity are implemented. Full graph resolution, lock
files, conflict handling, package signatures, and offline graph replay remain
open; repository and key-management policy remains cixd-owned.

## Workstream 4: qualification

Applicable issues: #64, #65, #77, #79, #81, #106.

Next deliverables are hostile archive/package fixtures, bounds and fuzz gates,
cancellation and resource limits, clean-host CI, and reproducibility gates.

## Workstream 5: production integration

Applicable issues: #35–#44 and #57–#85.

These require inputs not present in this repository: a versioned cixd protocol,
production sandbox and repository services, and the first authoritative CPDL
recipe corpus. Legacy shell recipes are the replacement target, not a CBS
runtime dependency. See [the integration blocker register](integration-blockers.md).

## Recommended order

1. Harden CIXPKG and refresh stale validation records.
2. Improve CLI diagnostics and machine-readable operation modes.
3. Add dependency, provenance, and signature foundations.
4. Add security, reproducibility, resource, and CI qualification.
5. Integrate cixd and migrate the authoritative recipe corpus.
