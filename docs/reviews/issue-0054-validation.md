# Issue #54 validation

Implemented CIXPKG v1 writer/reader primitives with self-describing magic,
identity, payload size, zstd compression, and bounded decompression. Round-trip
and malformed/truncated frame checks are covered by the TCC package test.
