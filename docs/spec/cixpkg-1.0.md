# CIXPKG v2 binary specification

CIXPKG v2 is the only format implemented by CBS. Readers must not accept the
old v1 magic (`CIXPKG\0\1`); there are no v1 artifacts in the supported cache.

CIXPKG carries integrity digests, not authenticity signatures. Signing keys,
revocation, and approval records belong to the repository/orchestrator trust
boundary; standalone CBS has no key store and must not invent one. A signed
publication therefore signs the complete CIXPKG bytes (or its repository
metadata) externally, and the orchestrator verifies that detached signature
before installation. The format deliberately has no signature field.

All integers are unsigned little-endian. The v2 header is 352 bytes and begins
with eight bytes `CIXPKG\0\2`, followed by `u64 header_size`, `u64
manifest_size`, and `u64 payload_size`. Bytes 32–95 contain the lowercase
ASCII SHA-256 digest of the uncompressed manifest; bytes 96–159 contain the
lowercase ASCII SHA-256 digest of the uncompressed regular-file payload. Bytes
160–223 contain the UTF-8 canonical package identity, NUL-padded; an identity
longer than 64 bytes cannot be represented and is a write failure, never a
truncation. Byte 224 is a u8 metadata-flags field; bit 0
(`CBS_CIXPKG_FLAG_FINALIZED`) records that an embedder-supplied finalization
policy completed before manifest generation. Unknown flag bits are invalid.
All other header bytes are reserved and zero-filled.

The manifest is UTF-8 text, sorted by unique path. Every entry carries an
octal mode and normalized ownership (`uid=0 gid=0`):

* regular file: `f mode 0 0 size digest path\n`
* directory: `d mode 0 0 path\n`
* symbolic link: `l mode 0 0 target-hex path\n`

Regular-file bytes are concatenated in manifest order. Directories, including
empty directories, have no payload bytes. Symbolic-link targets are stored
verbatim as lowercase hexadecimal bytes. Absolute targets are interpreted
relative to the assembled image root and may be dangling at package time.
Relative targets must remain within that root when resolved from the link's
parent; links that climb above the root are rejected. Devices, FIFOs, sockets,
hardlinks, and setuid/setgid modes are rejected. Other mode permission bits are
preserved. Package readers also require ownership to be exactly root and reject
unsafe modes in crafted manifests.

Readers validate magic/version, bounds, both section digests, sorted unique
paths, safe paths, entry types, ownership/modes, symlink confinement, and each
file digest before exposing an entry. A short read, overflow, digest mismatch,
unsafe path, unsafe link, or unknown type is corruption and reports
`CIXPKG-E4001`; no partial extraction is retained. Extraction verifies the
complete artifact before writing a temporary directory, preserves file and
directory modes, preserves symlinks, and publishes the directory atomically.

Compression is fixed zstd level 19 with checksum enabled. A future format
version is required to change these parameters or add reader semantics.
