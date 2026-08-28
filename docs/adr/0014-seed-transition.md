# ADR-0014: Seed-to-CBS-managed transition

Stage zero uses only the ADR-0011 seed. After the first verified CBS package
is installed, CBS becomes the sole owner of subsequent library upgrades;
replacement requires provenance, digest, reproducibility, and rollback checks.
The seed remains available for recovery and is never silently replaced.
