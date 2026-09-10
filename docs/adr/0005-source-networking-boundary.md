# ADR-0005: Source networking boundary

- Status: Accepted (revised for standalone transport)
- Date: 2026-08-28
- Scope: CBS source and artifact byte acquisition

## Decision

CBS supports two transport modes. The standalone CLI uses the system libcurl
runtime for HTTP/HTTPS source acquisition. cixd may provide the same
`CbsFetchService` callback when it owns transport policy. CBS never invokes
`curl`, `wget`, or another network utility as a subprocess.

In standalone mode, libcurl owns TLS trust anchors, certificate validation,
hostname verification, proxy policy, and HTTP redirects. CBS restricts the
protocol set to HTTP/HTTPS, applies bounded connect/transfer timeouts and
redirects, and reports transport errors. The standalone CLI may add a private
CA with `--ca-file`, but it must not disable peer or hostname verification. In
cixd mode, cixd owns those policies
and the base boundary returns a bounded byte stream or structured failure.

CBS remains responsible for source identity and integrity: it selects declared
mirrors, asks `cixd` for bytes, computes the declared SHA-256 itself, rejects
mismatches, and exposes no named source until the issue #8 all-sources gate
passes. A transport success is never an integrity success.

Transport is still subordinate to CBS source identity and integrity: every
download is written to a temporary file, hashed by CBS, and atomically moved
into the digest-keyed cache only after verification. A transport success is
never an integrity success.

## Rationale

Standalone builds need a usable source path without requiring a daemon. Using
libcurl avoids shell or utility execution while reusing a maintained TLS/HTTP
implementation. cixd remains available when centralized policy and audit
logging are required.

Approved base libraries may be used inside `cixd` under ADR-0002; that choice
does not create a second CBS networking implementation or permit an ambient
fallback.

## Consequences

- Standalone CBS has a runtime dependency on libcurl for cache misses.
- `cixd` remains responsible for centralized transport policy and observability
  when its callback is supplied.
- CBS tests use a local HTTP fixture and deterministic fetch-service fixtures;
  no public network is required.
- A service outage is a source-preparation failure, never silently bypassed.
