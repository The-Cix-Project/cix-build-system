# CIXPKG v1 binary specification

All integers are unsigned little-endian. The header begins with eight bytes
`CIXPKG\0\1`, followed by `u64 header_size`, `u64 manifest_size`, `u64
payload_size`, and 32-byte SHA-256 digests for the header, manifest, and
payload. Header metadata is UTF-8 `key=value\n` records and includes canonical
identity and runtime dependencies. The manifest is sorted by path and contains
one record per entry: path length, path bytes, type, mode, size, and digest.
The payload concatenates regular-file bytes in manifest order.

Readers must validate magic/version, bounds, all three section digests, sorted
unique paths, and each file digest before exposing an entry. A short read,
overflow, digest mismatch, unsafe path, or unknown required type is corruption
and reports `CIXPKG-E4001`; no partial extraction is retained. The standalone
`extract` operation verifies the complete artifact before writing into a
temporary directory, preserves regular-file modes, and publishes the directory
atomically. Compression is fixed zstd level 19 with checksum enabled; a future
format version is required to change parameters.
