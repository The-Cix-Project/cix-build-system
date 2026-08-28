# Issue #15 validation: digest kinds

CBS now computes recipe SHA-256 digests over the normalized `.cbs` definition,
reports declared source digests, and computes an artifact SHA-256 when an
artifact path is supplied. `cbs inspect PACKAGE.cbs [ARTIFACT]` is the single
reporting surface. The TCC build and regression suite pass cleanly.
