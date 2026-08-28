# ADR-0008: CBS command surface

- Status: Accepted
- Date: 2026-08-28

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

The CLI remains a thin adapter because cixd owns sandbox creation, transport,
and image transactions. No second REST or daemon implementation is introduced.
