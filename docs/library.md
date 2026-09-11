# CBS library boundary

`libcbs.a` contains the standalone CBS engine used by the `cbs` executable.
Consumers include `cbs/cbs.h` and may use the functions declared in that
header for recipe validation, source fetching, archive extraction, execution,
manifests, and CIXPKG creation and verification.

The library does not create a sandbox, daemon connection, service lifecycle, or
transaction. `cbs_sandbox_run`, `cbs_daemon_request`, `cbs_service_health`,
`cbs_transaction`, and signature verification are explicit callback seams for
the embedding platform. A consumer owns the sandbox and supplies those hooks
when it needs them.

Build and install with `make` and `make install PREFIX=/usr/local`.
