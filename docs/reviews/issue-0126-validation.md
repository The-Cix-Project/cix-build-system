# Issue #126: build-plan phase capacity

## Result

Validated and closed in the standalone CBS implementation.

CPDL 0.1 has five ordered, optional phases: `prepare`, `configure`, `build`,
`check`, and `install`. The public `CbsBuildPlan` uses the named
`CBS_MAX_PHASES` constant, and the planner fails closed if a malformed AST
contains more entries; it no longer carries a dead fixed list of phase kinds.

The validator now reports `CPDL-E3004` at the sixth phase, so a recipe author
gets a direct diagnostic instead of discovering the limit only during plan
construction. Duplicate phase names are still reported independently, and the
planner test covers both the exact-capacity and overflow cases.

The limit is intentional language semantics, not an arbitrary array detail:
the CPDL grammar defines exactly these five phase positions and their order.
