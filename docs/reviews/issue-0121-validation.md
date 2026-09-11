# Issue #121 validation: hostile-input corpus

The repository now has a deterministic CIXPKG hostile corpus in
`tests/fuzz-test.c`. It creates a valid v2 artifact, mutates protected header,
manifest-compression, and payload-compression bytes at multiple offsets, and
requires both verification and extraction to reject every mutation. The
identity field is intentionally excluded because it is a bounded, mutable
metadata field rather than a digest-protected section.

The existing archive tests cover unsupported entries, traversal, and symlink
attacks; parser-validation covers malformed CPDL; and CIXPKG tests cover
truncation and digest corruption. Together they exercise the untrusted readers
without requiring a third-party fuzzing engine in the build image.
