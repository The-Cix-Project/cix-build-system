# CIXPKG v1 binary specification

All integers are unsigned little-endian. The v1 header is 352 bytes and begins
with eight bytes `CIXPKG\0\1`, followed by `u64 header_size`, `u64
manifest_size`, and `u64 payload_size`. Bytes 32–95 contain the lowercase
ASCII SHA-256 digest of the uncompressed manifest; bytes 96–159 contain the
lowercase ASCII SHA-256 digest of the uncompressed regular-file payload. Bytes
160–286 contain the UTF-8 canonical package identity, NUL-padded. The
remaining header bytes are reserved and zero-filled.

The manifest is UTF-8 text, sorted by path, with one record per regular file:
`f mode size digest path\n`. The payload concatenates regular-file bytes in
manifest order. A file’s payload offset is implicit: it is the sum of the
sizes of preceding manifest records. Directories are represented by the
presence of parent paths and have no record or payload range in v1.

Readers must validate magic/version, bounds, both section digests, sorted unique
paths, and each file digest before exposing an entry. A short read,
overflow, digest mismatch, unsafe path, or unknown required type is corruption
and reports `CIXPKG-E4001`; no partial extraction is retained. The standalone
`extract` operation verifies the complete artifact before writing into a
temporary directory, preserves regular-file modes, and publishes the directory
atomically. Compression is fixed zstd level 19 with checksum enabled; a future
format version is required to change parameters.
