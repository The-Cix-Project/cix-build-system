# Issue #97 validation

Machine-readable diagnostics remain planned for scriptable standalone use.
`cbs explain RECIPE.cbs` now provides a non-executing, human-readable phase
plan after full validation, covering the first explain-mode requirement.
`cbs explain RECIPE.cbs --json` additionally exposes that plan as a stable
machine-readable object. `cbs check` and `cbs validate` accept `--json` and
emit newline-delimited diagnostic objects containing location, severity, code,
category, and message; the default human-readable output is unchanged.
