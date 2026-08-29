# Issue #114 validation

Added `cbs_build_standalone`, which parses and validates a recipe, prepares the
workspace, fetches and extracts declared sources through the verified service
boundary, constructs identity/context, executes the deterministic phase plan,
and emits a manifest-backed CIXPKG. The CLI build command now uses this
standalone pipeline; cixd is not required for the API.
