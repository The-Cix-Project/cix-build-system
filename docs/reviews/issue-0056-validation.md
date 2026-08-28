# Issue #56 validation

`cbs_build_package` now validates a CPDL recipe, emits a deterministic staged
manifest, and writes a verified CIXPKG through one pipeline. The package test
exercises this path under the TCC build.
