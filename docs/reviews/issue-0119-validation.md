# Issue #119 validation: CIXPKG authenticity boundary

CIXPKG v2 deliberately does not contain a signature field. Its manifest and
payload digests detect corruption and are checked before extraction, while
authenticity requires a key, revocation policy, and approval record that
standalone CBS does not own.

The repository/orchestrator signs the complete artifact bytes or its package
metadata and verifies that detached signature before installation. This avoids
two competing trust systems and keeps key lifecycle outside the build engine.
The old v1 header complaint about an unused signature area does not apply to
the v2 header: bytes 96–159 are the checked payload digest.
