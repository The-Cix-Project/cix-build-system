# Issue #55 validation

`cbs_manifest_write` recursively walks a staged tree, records regular-file
type, mode, size, and SHA-256, sorts paths canonically, and emits deterministic
records. The manifest test exercises tree emission under TCC.
