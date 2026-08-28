# ADR-0005: Source networking boundary

- Status: Accepted
- Date: 2026-08-28
- Scope: CBS source and artifact byte acquisition

## Decision

CBS consumes bytes from the lower Cix service (`cixd`) through the base
boundary. CBS does not implement HTTP, TLS, redirects, proxy negotiation,
certificate stores, or network retries, and it does not invoke `curl`, `wget`,
or another network utility.

`cixd` owns transport policy, TLS trust anchors, certificate validation,
hostname verification, proxy policy, redirect limits, retry/backoff policy, and
network audit logs. The base boundary must return the requested URL, selected
mirror, response metadata, and a bounded byte stream or a structured failure.
The exact wire/IPC ABI belongs to the Cix base and must be version-pinned before
CBS source fetching is enabled.

CBS remains responsible for source identity and integrity: it selects declared
mirrors, asks `cixd` for bytes, computes the declared SHA-256 itself, rejects
mismatches, and exposes no named source until the issue #8 all-sources gate
passes. A transport success is never an integrity success.

This is one implementation path. CBS has no fallback to host `curl` or another
library when the Cix service rejects or cannot fetch a URL. The failure is
reported with the source name, URL/mirror, and service error.

## Rationale

Putting TLS in CBS would enlarge the TCC-rooted trusted base with protocol,
certificate, redirect, and retry code. Keeping it in `cixd` centralizes the
already-required control-plane policy and makes the observed curl redirect
failure diagnosable in one service. The split also keeps the package engine
light while retaining independent CBS checksum verification.

Approved base libraries may be used inside `cixd` under ADR-0002; that choice
does not create a second CBS networking implementation or permit an ambient
fallback.

## Consequences

- CBS source acquisition cannot operate before the pinned Cix fetch boundary is
  available.
- `cixd` becomes responsible for TLS updates and network observability.
- CBS tests use a deterministic fetch-service fixture, not live network calls.
- A service outage is a source-preparation failure, never silently bypassed.
