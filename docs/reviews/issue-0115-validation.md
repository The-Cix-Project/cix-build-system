# Issue #115 validation: sectioned CIXPKG container

The CIXPKG writer now emits a fixed 352-byte section table containing the
manifest and payload lengths, independent section digests, and canonical
identity. Standalone package creation emits the complete staged regular-file
payload. Verification enforces the magic/version, header size, bounded section
lengths, zstd decode lengths, section digests, manifest ordering, payload ranges,
and per-file digests before returning identity.

TCC strict compilation and the complete regression suite pass.
