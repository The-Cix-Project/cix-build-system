# Issue #31 validation: stage reproducibility

CBS provides a byte-level comparison gate for stage outputs. Any differing
entry or byte causes failure; callers report the named paths rather than
silently accepting a bootstrap mismatch. TCC regression coverage proves equal
outputs pass.
