# ADR-0007: CBS v1 sandbox and dependency observation

- Status: Accepted
- Date: 2026-08-28

## Decision

Every build executes in a private staged tree with declared source, build, and
destination roots. Filesystem operations are confined to those roots; writes to
the host root and undeclared paths fail. Network access is denied to build
phases. Source acquisition occurs before phases through either standalone
libcurl transport or the cixd fetch-service boundary. Process creation is
limited to declared `run` operations, with `$jobs` and timeout limits enforced.

CBS records the phase exit status, files, modes, ownership, symlinks, and
runtime ELF `DT_NEEDED` entries produced in the staged tree. Declared runtime
dependencies are compared with observed ELF dependencies; missing declarations
are errors, while extra declarations are warnings promoted to errors by the
reproducible build gate.

## Deferred

Kernel-level namespaces, cgroups, seccomp, network policy implementation, and
dynamic tracing of interpreter/plugin loads are cixd responsibilities and are
not claimed by CBS v1. Static observation does not guarantee detection of
optional `dlopen` paths or runtime configuration lookups.

## Consequence

The sshd/PAM class of omission becomes a build-time validation failure when the
linked ELF dependency is absent from the declaration. The boundary remains
auditable: CBS enforces staged-tree and observation invariants; cixd supplies
strong OS isolation.
