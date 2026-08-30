# ADR-0012: CIXPKG staged-payload indexing

## Decision

The CIXPKG manifest remains the canonical ordering of package entries. The
payload section concatenates regular-file bytes in that order. Each regular
file record carries an unsigned offset and length into the payload plus the
file digest already recorded by the manifest. Directories have no payload
range. Readers must prove that ranges are within the payload, non-overlapping,
and reproduce the recorded digest before exposing or installing a file.

The writer will derive both the index and payload from the same manifest walk;
there is no second filesystem traversal with independent ordering rules. The
payload is compressed with the format's fixed zstd policy, while manifest and
payload digests cover their uncompressed section bytes.

## Consequences

This permits bounded, deterministic extraction and makes corruption detectable
before destination mutation. It requires the package API to accept a staged
root (rather than only a manifest path) and requires verifier tests for overlap,
overflow, truncated payloads, and per-file digest mismatches.
