# ADR-0037: CIXPKG staged-payload indexing

## Decision

The CIXPKG manifest remains the canonical ordering of package entries. The
payload section concatenates regular-file bytes in that order. In v1, each
regular-file record carries its length and digest; its offset is implicit from
the sum of preceding record lengths. Directories have no payload range.
Readers must prove that the derived ranges are within the payload,
non-overlapping, and reproduce the recorded digest before exposing or
installing a file.

The writer will derive both the index and payload from the same manifest walk;
there is no second filesystem traversal with independent ordering rules. The
payload is compressed with the format's fixed zstd policy, while manifest and
payload digests cover their uncompressed section bytes.

## Consequences

This permits bounded, deterministic extraction and makes corruption detectable
before destination mutation. It requires the package API to accept a staged
root (rather than only a manifest path) and requires verifier tests for overlap,
overflow, truncated payloads, and per-file digest mismatches.

## Numbering

Recorded as ADR-0012 on 2026-08-31 and renumbered to 0037 on 2026-09-19:
ADR-0012 was already taken by the stage-zero build entry point, decided on
2026-08-28 and cross-referenced as 0012 by the issue #29 validation record.
The decision itself is unchanged.
