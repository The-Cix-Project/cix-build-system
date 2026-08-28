# Issue #38 validation

The canonical `CbsManifestEntry` carries path, type, mode, size, and digest;
lexical ordering is centralized in `cbs_manifest_compare`. Derived-image
composition can consume this manifest without reopening package payloads.
