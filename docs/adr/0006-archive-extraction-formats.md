# ADR-0006: CBS v1 archive extraction formats

- Status: Accepted
- Date: 2026-08-28

## Context

CBS must extract upstream source archives without invoking an ambient external
tool. The issue requested enumeration of `recipes/package/*/*/build.sh`, but
that corpus is not present in this repository yet; the decision is therefore a
deliberate v1 baseline and must be revisited when the recipes are imported.

## Decision

CBS v1 supports these archive formats through linked libraries:

- POSIX tar streams compressed with gzip (`.tar.gz`, `.tgz`);
- POSIX tar streams compressed with bzip2 (`.tar.bz2`, `.tbz2`);
- POSIX tar streams compressed with xz (`.tar.xz`, `.txz`); and
- ZIP archives (`.zip`).

The extraction API receives a verified source byte stream and an explicitly
confined destination. Format detection is by the declared source filename and
validated magic bytes; a mismatch is an error.

## Deferred formats

LZMA-alone, zstd-compressed tar, lzip, 7z, RAR, cpio, ar/deb, and ISO images
are deferred until an imported recipe corpus demonstrates a v1 requirement.
They are not silently routed to `tar`, `unzip`, or another host executable.

## Failure mode

An unsupported or mismatched archive produces `CPDL-E6001` in the extraction
category and names the source, declared filename, and detected format (or
`unknown`). No destination mutation is committed on that failure.

## Consequences

The v1 library set stays small and auditable while covering the common source
archive families. Adding a format requires a follow-up ADR or amendment,
library boundary, fixtures, and bounds-tested extraction behavior.
