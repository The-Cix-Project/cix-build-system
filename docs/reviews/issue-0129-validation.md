# Issue #129: embedder-supplied finalization policy

## Result

Implemented and tested in standalone CBS.

Embedders can call `cbs_build_standalone_with_cache_policy()` with a
`CbsFinalizePolicy` callback. CBS invokes it on the recipe's staged
destination after all phases, including `install`, have completed and before
`cbs_manifest_write()` runs. A failed callback fails the build and no artifact
is published. CPDL has no syntax for this callback, so recipes cannot disable
or replace platform policy.

When the callback succeeds, CIXPKG v2 sets `CBS_CIXPKG_FLAG_FINALIZED` in the
header metadata flags at offset 224. The verifier rejects unknown flags. The
existing standalone APIs remain wrappers with no finalizer for source
compatibility; an orchestrator that needs platform normalization opts into the
policy-aware API explicitly.

The callback receives the staged root and may normalize ownership, modes, or
other platform policy before the manifest is calculated. The callback is the
policy boundary; CBS does not embed a recipe-defined policy language.
