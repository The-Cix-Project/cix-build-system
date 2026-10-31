# CBS delivery roadmap

Release baseline: **v0.1.128**. The complete standalone implementation and its
regression gate are shipped and tagged. The improvement queue is tracked in
the issue tracker; the workstreams below describe ownership boundaries and
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
limits, and reproducibility gates are implemented. A clean-host CI image was
assessed as useful qualification and **declined by the owner on 2026-10-09**:
the gate is run by hand, on a checkout, for each release (see the
[release guide](guides/ci-and-release.md)). It is recorded here as the
engineering assessment it was, not as work to pick up.

## Workstream 5: production integration

Applicable issues: #35–#44 and #57–#85.

These require inputs not present in this repository: a versioned cixd protocol,
and production sandbox and repository services. The recipe corpus is no longer
among them — it was converted to CPDL by 2026-10-09 and lives in the recipe
repository, not here. See [the integration blocker register](integration-blockers.md).

## Language completeness (2026-09-18)

The CPDL gaps the recipe-corpus audit recorded are closed: `each` iteration
(ADR-0035), `replace ... until`, `stage library` (ADR-0036), `require
symlink`, and portable-case environment names. The audit is closed (see
[the corpus audit](cpdl-corpus-audit.md)): `gcc` is converted and validates
under an explicit `toolchain` exception, and 272 recipes declared `capability`
as of 2026-10-09. `output` blocks (ADR-0038, v0.1.119) later let one recipe emit
several packages from one build.

## Recommended order

1. Harden CIXPKG and refresh stale validation records.
2. Improve CLI diagnostics and machine-readable operation modes.
3. Add dependency, provenance, and signature foundations.
4. Add security, reproducibility, and resource qualification. (A clean-host CI
   image, once listed here, was declined on 2026-10-09; see Workstream 4.)
5. Integrate cixd.
