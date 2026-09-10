# Issue #11 validation: source networking boundary

- Issue: `#11 Decide whether CBS performs networking itself or consumes a lower Cix service`
- Date: 2026-08-28
- Result: Pass
- Decision: [ADR-0005](../adr/0005-source-networking-boundary.md)

The original ADR-0005 decision assigned network transport exclusively to
`cixd`. That decision was revised to permit the standalone CLI's libcurl
transport while retaining the same `CbsFetchService` boundary. cixd remains
the preferred owner of centralized TLS, proxy, redirect, retry, and audit
policy when it supplies the callback. CBS independently verifies every source
against its declared SHA-256 before publishing `$source.NAME`.

CBS uses libcurl through an in-process runtime adapter and never invokes
`curl`/`wget` as a subprocess. Service or transport failure remains a named
source preparation failure, and source failures remain observable without
weakening the independent integrity check.

The revised ADR records standalone libcurl as the transport for daemon-free
builds while retaining cixd for centralized policy. Tests use a local HTTP
fixture and deterministic fetch-service fixtures; no public network is needed.

The complete warning-clean TCC regression gate passes unchanged; this ticket
adds the standalone transport without invoking a host network utility.
