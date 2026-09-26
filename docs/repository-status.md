# CBS repository status

Current release: **v0.1.68**. The branch and tag are clean, and the full
`make -j1 test` gate passes for this release.

This file reconciles the repository with the historical issue tracker. CBS is
the standalone CPDL engine and CIXPKG producer; it is not the cixd daemon and
does not claim to replace cixd's sandbox, image, repository, or deployment
services.

Implemented in this repository are the parser/validator, direct execution,
workspace and source handling, typed CIXPKG v2 creation/verification/extraction,
hostile-input tests, child resource limits and interrupt cleanup, ELF runtime
dependency observation, and tested embedding seams. The CIXPKG format carries
integrity digests; detached signatures and approval/revocation policy belong to
the repository/orchestrator.

Still outside this repository are cixd integration, production sandbox and
cgroup enforcement, image transaction/rollback ownership, package graph
resolution, repository key lifecycle, and migration of the authoritative shell
recipe corpus. Those require the cixd repository or operator-owned build-image
state and must not be represented as completed CBS functionality.

Historical validation records describe the state when their issues were
closed. The issue tracker is the authority for future work; as of v0.1.68
there is no open repository backlog. This status file prevents old acceptance
language from being mistaken for a claim that standalone CBS owns the whole
platform.
