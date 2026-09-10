# Issue #26 validation: safe installation boundary

Verified staged files can be installed atomically with `cbs_install_atomic`,
which applies the manifest mode before rename. Archive extraction rejects
unsafe entry types and traversal. CIXPKG extraction verifies the manifest,
payload, and per-file digests before creating a temporary destination, preserves
regular-file modes, and atomically publishes the extracted tree.
