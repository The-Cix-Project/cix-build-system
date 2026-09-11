# ADR-0033: Select artifact format from the immutable recipe revision

## Status

Accepted

## Decision

An immutable recipe revision declares exactly one artifact format. The
installer selects the corresponding typed reader from that declaration; it
does not probe for one format and fall back to another. A revision declaring
`cixpkg` produces a `.cixpkg` artifact, while a revision declaring `tar.gz`
continues to identify its `.tar.gz` artifact. The artifact name includes its
format suffix, so no existing content-addressed name changes meaning.

Existing `.tar.gz` revisions are not a migration state and need no special
fallback path: their revision declares `tar.gz`. CPDL format-field syntax and
the repository-side format declaration are implemented as part of #141 before
the first real recipe switches to CIXPKG.

The CIXPKG cutover gate is the typed-tree round trip (#128), followed by the
byte-identical reproducibility gate (#135). No real package is switched before
both gates pass.

## Consequences

There is one data-selected installation path, no silent corruption masking by
probing, and no dual-path sunset mechanism. Format migration is performed by
publishing a new immutable recipe revision.
