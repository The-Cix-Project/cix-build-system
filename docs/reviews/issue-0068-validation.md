# Issue #68 validation

The single `CbsDaemonRequest`/`cbs_daemon_request` boundary is the integration
surface for cixd. It carries authenticated operation payloads and bounded
responses; transport, retries, and endpoint configuration remain supplied by
the deployment adapter. A real cixd contract fixture is required before final
production qualification.
