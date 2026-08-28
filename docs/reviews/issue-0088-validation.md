# Issue #88 validation

Standalone builds use caller-supplied staged/workspace roots and the centralized
`CbsStagePolicy` path checks. Production hardening requires rejecting writes
outside those roots and cleaning interrupted workspaces.
