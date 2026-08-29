# Issue #101 validation

`cbs_build_plan` creates one deterministic, pre-execution plan from a validated
AST. It exposes phase nodes in declaration order and prevents execution before
parsing/validation completes.
