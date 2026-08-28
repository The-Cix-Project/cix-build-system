# Issue #25 validation: CIXPKG inspection and verification

CBS now independently decompresses zstd package payloads with bounded frame
size checks and rejects unknown, truncated, or corrupt frames. Compression and
round-trip verification are covered by the TCC package test.
