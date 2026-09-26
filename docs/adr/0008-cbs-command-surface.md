# ADR-0008: CBS command surface

- Status: Accepted
- Date: 2026-08-28

## Current implementation note (v0.1.64)

This historical command-surface proposal is not the current CLI contract.
`cbs --help` currently exposes `check`/`validate`, `explain`, `build`,
`inspect`, `verify`, and `extract`; it does not expose `package` or `install`.
Artifact installation and image transactions remain cixd responsibilities.
The current command examples are maintained in the user manual and the
standalone runbook.

CBS exposes six operations through one internal API and a thin CLI: `check`
(parse/validate), `build` (produce staged tree), `package` (emit CIXPKG),
`install` (request cixd transaction), `inspect` (read metadata), and `verify`
(verify artifact digest/manifest). Exit 0 means success; exit 2 is invocation
error; exit 3 is definition/operation failure; exit 4 is artifact verification
failure.

`inspect` and `verify` accept an artifact alone and never require a CPDL
definition or execute recipe phases. `install` prefers a verified artifact; a
build fallback is permitted only when the caller's explicit policy allows it.
CBS does not choose host policy.

The CLI remains a thin adapter because cixd owns optional sandbox creation and
image transactions. Standalone CBS owns its approved libcurl source transport;
when cixd is present, it may supply the fetch-service callback instead. No REST
or daemon implementation is introduced in CBS.
