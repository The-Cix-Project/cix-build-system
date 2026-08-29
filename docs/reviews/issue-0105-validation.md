# Issue #105 validation

The existing `CbsExecutionContext` and resolver provide one source of truth for
identity, workspace, jobs, environment, and named-source interpolation. The
standalone runner passes this context to every phase; undefined references are
rejected during validation.
