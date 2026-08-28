# ADR-0021: Unified artifact repository

CBS uses Cix's existing artifact-server URL, bearer authentication, package and
image prefixes, and git-backed recipe sync. CIXPKG digests and manifests extend
that model; no parallel repository protocol is introduced.
