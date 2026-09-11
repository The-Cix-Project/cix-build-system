# Issue #127: parallel CIXPKG implementations

## Result

Validated and closed in the standalone CBS implementation.

The obsolete header-only/payload-less `cbs_cixpkg_write` and
`cbs_cixpkg_verify` APIs are no longer present. Production package creation,
verification, extraction, fuzz coverage, and the package tests all use the
single sectioned CIXPKG v2 implementation: `cbs_cixpkg_write_tree` and
`cbs_cixpkg_verify_tree`.

The old test path was removed rather than made to understand v2. The package
test now verifies a real tree-produced artifact, including payload corruption
and identity checks, so a passing test covers what CBS actually writes.

The standalone build API has also been consolidated: the cache-aware function
is the implementation and `cbs_build_standalone` is only its documented
compatibility wrapper. There are no parallel format writers or readers, and
the v2 reader intentionally does not accept a legacy format.
