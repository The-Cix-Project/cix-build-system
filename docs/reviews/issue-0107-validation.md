# Issue #107 validation

The standalone pipeline applies `CbsStagePolicy` and deterministic manifest
generation before packaging. Unsafe paths, entries, and empty outputs fail
before an artifact is emitted.
