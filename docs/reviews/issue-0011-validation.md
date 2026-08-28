# Issue #11 validation: source networking boundary

- Issue: `#11 Decide whether CBS performs networking itself or consumes a lower Cix service`
- Date: 2026-08-28
- Result: Pass
- Decision: [ADR-0005](../adr/0005-source-networking-boundary.md)

ADR-0005 assigns network transport to `cixd`, the lower Cix control-plane
service. `cixd` owns TLS trust anchors, certificate validation, hostname and
proxy policy, redirects, retries, and network audit logs. CBS consumes a pinned,
versioned byte-stream boundary and independently verifies every source against
its declared SHA-256 before publishing `$source.NAME`.

CBS has no HTTP/TLS implementation, no network utility invocation, and no
`curl`/library fallback. Service or transport failure remains a named source
preparation failure. The boundary returns selected mirror and response metadata
so source failures remain observable without moving TLS policy into CBS.

The ADR explicitly records the observed curl redirect failure as a reason to
centralize diagnosis in `cixd`, while retaining CBS's independent integrity
check. It also records the prerequisite of a pinned Cix ABI and deterministic
fetch-service fixtures before source fetching is enabled.

The complete warning-clean TCC regression gate passes unchanged; this ticket
introduces no code or parallel network path.
