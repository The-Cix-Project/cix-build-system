# Issue #115 validation: sectioned CIXPKG container

The CIXPKG writer emits the fixed 352-byte v1 header containing the manifest
and payload lengths, independent ASCII SHA-256 section digests, and canonical
identity. Standalone package creation emits the complete staged regular-file
payload. Verification enforces the magic/version, header size, bounded section
lengths, zstd decode lengths, section digests, manifest ordering, derived
payload ranges, and per-file digests before returning identity.

TCC strict compilation and the complete regression suite pass.
