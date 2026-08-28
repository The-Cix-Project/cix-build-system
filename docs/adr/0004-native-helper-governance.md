# ADR-0004: Governance for package-specific native helpers

- Status: Accepted
- Date: 2026-08-28
- Owners: Cix project maintainers
- Scope: Exceptional package-specific native helpers outside CPDL 0.1

## Decision

A package-specific native helper is permitted only when all of the following
are true:

1. CPDL 0.1 cannot express the operation without weakening a security or
   reproducibility invariant;
2. the operation is genuinely package-specific rather than a reusable file,
   process, source, archive, assertion, or metadata primitive;
3. the helper has a bounded interface, deterministic inputs/outputs, and no
   shell evaluation or ambient environment access;
4. the package maintainer supplies a threat model, test fixture, failure model,
   ownership plan, and removal plan; and
5. maintainers approve the exception before it is merged.

Requests are refused when a helper duplicates an existing CPDL operation, wraps
a host utility that CBS should own, introduces a second parser or language, or
is proposed merely for convenience. A repeated need across unrelated packages
is evidence for a general CPDL operation, never justification for another
helper.

## Approval and audit record

The requesting package issue is the design record. Approval requires two
maintainer reviews, one focused on security and one on reproducibility. The
approved helper is then listed in `docs/native-helpers.md` with its issue,
commit, owner, scope, interface, tests, and review date. The registry is the
authoritative inventory; an unlisted helper is non-conforming.

Helpers remain package dependencies, not CBS dependencies. They cannot alter
the TCC-only compiler boundary, shell prohibition, source verification, path
confinement, or deterministic package identity.

## Periodic review

Maintainers review the registry quarterly and at every release. In addition,
each review searches closed package issues and recipe migrations for repeated
helper requests. Two materially similar requests from unrelated packages
trigger a CPDL design issue; three trigger a default presumption that the
helper must be replaced by a general CPDL operation before either package is
accepted.

The review records its date, search scope, matches, decision for each entry,
and follow-up issue in the registry. This makes repetition observable rather
than dependent on memory.

## Consequences

The default remains no native helpers. Exceptional mechanisms are reviewable,
bounded, and removable, while repeated requirements are driven back into the
single CPDL implementation instead of forming a second language.
