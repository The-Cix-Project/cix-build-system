# Issue #116 validation: staged payload sections

The standalone package pipeline now serializes every staged regular-file byte
into the CIXPKG v2 payload section in canonical manifest order. The header
records the uncompressed payload size and SHA-256 digest; verification checks
the section digest, each manifest range, and each per-file digest before
extraction. Files are written only after the complete artifact has verified and
the destination is published atomically.

`tests/package-test.c`, `tests/typed-package-test.c`, and the hostile mutation
corpus cover payload extraction, round-trip bytes, truncation, digest changes,
and malformed payload data. The full `make test` suite passes.
