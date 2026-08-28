# Issue #17 validation: jobs ceiling

CBS exposes `cbs_effective_jobs`, applying the requested value against shared
CPU and administrator ceilings. A zero request defaults to one; concurrency
cannot multiply the configured ceiling. TCC regression coverage verifies the
clamping policy.
