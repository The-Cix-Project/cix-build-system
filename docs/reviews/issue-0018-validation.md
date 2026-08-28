# Issue #18 validation: staged-tree policy

`CbsStagePolicy` is the single policy data object for empty, absolute, and
parent-traversal path rejection. `cbs_validate_stage_path` applies it
consistently; violations are rejected before staging and tests name the
offending classes. TCC tests pass with the standard warning policy.
