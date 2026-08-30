# Issue #115 validation: sectioned CIXPKG container

The CIXPKG writer now emits a fixed 352-byte section table containing the
manifest and payload lengths, independent section digests, and canonical
identity. The current public writer API supplies the manifest section and
therefore emits a zero-length payload section; the verifier rejects non-zero
payloads until the payload-producing API is expanded. Verification enforces
the magic/version, header size, bounded manifest length, zstd decode length,
and manifest digest before returning identity.

TCC strict compilation and the complete regression suite pass.
