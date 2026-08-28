# Issue #26 validation: safe installation boundary

Verified staged files can be installed atomically with `cbs_install_atomic`,
which applies the manifest mode before rename. Archive extraction rejects
unsafe entry types and traversal; installation never exposes a partially copied
file.
