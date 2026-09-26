# ADR-0033: Select artifact format from the immutable recipe revision

## Status

Superseded in part by CPDL 0.1's single supported artifact format. CBS now
rejects `format "tar.gz"` during validation; existing tar archives remain
source inputs, not CBS package outputs.

Accepted

## Decision

An immutable recipe revision declares exactly one artifact format. The
standalone CPDL 0.1 implementation supports only `cixpkg`, which produces a
`.cixpkg` artifact. The installer does not probe for one format and fall back
to another.

Tar archives may still be declared as source URLs and extracted during a
`cbs` build; they are not valid artifact formats.

The CIXPKG cutover gate is the typed-tree round trip (#128), followed by the
byte-identical reproducibility gate (#135). No real package is switched before
both gates pass.

## Consequences

There is one data-selected installation path, no silent corruption masking by
probing, and no dual-path sunset mechanism. Format migration is performed by
publishing a new immutable recipe revision.
