# CBS library boundary

`libcbs.a` contains the standalone CBS engine used by the `cbs` executable.
Consumers include the installed `cbs/cbs.h`. It is the stable embedding
surface for standalone builds, manifests, CIXPKG verification, source fetch
adapters, dependency observation, and signature adapters. Recipe lexer/parser
tokens, AST node layouts, and other implementation details remain private to
CBS and are not installed as library ABI.

The library does not create a sandbox, daemon connection, service lifecycle, or
transaction. `cbs_sandbox_run`, `cbs_daemon_request`, `cbs_service_health`,
`cbs_transaction`, and signature verification are explicit callback seams for
the embedding platform. A consumer owns the sandbox and supplies those hooks
when it needs them.

Build and install with `make` and `make install PREFIX=/usr/local`.
