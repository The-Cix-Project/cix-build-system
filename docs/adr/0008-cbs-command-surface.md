# ADR-0008: CBS command surface

- Status: Accepted
- Date: 2026-08-28

## Current implementation note (v0.1.126)

This historical command-surface proposal is not the current CLI contract.
`cbs --help` currently exposes `check`/`validate`, `explain`, `build`,
`package`, `fingerprint`, `doctor`, `inspect`, `verify`, `extract`, `list`,
`diff` and `revise`, plus `--version`, `--help` and `--capabilities`. It does
**not** expose `install`: artifact installation and image transactions remain
cixd responsibilities. An earlier version of this note said `package` was not
exposed either; that was wrong — `cbs package ROOT --name … --output FILE`
packages a tree a caller assembled. The current command examples are
maintained in the user manual and the standalone runbook.

Exit statuses are as this ADR fixes them: 0 success, 2 invocation error, 3
definition/operation failure, 4 artifact verification failure. Where a build's
event stream is written is an invocation concern and not part of this surface:
`--events` selects the standard-error reporter, while `--events-fd` and
`--events-file` name an independent JSONL destination (issue #282).

Options added to the surface since, each an invocation concern rather than a
change to the decision above:

- `build --output-dir DIR` writes one canonically named CIXPKG per declared
  `output` instead of one artifact at a named path, and `--output FILE` is
  refused when a recipe declares several (#299, ADR-0038, v0.1.119).
- `fingerprint --finalize-command CMD` (#303, v0.1.123), and
  `fingerprint --prune-policy FILE` and `fingerprint --firmware-root DIR`
  (#305, v0.1.124). All three already existed on `build` and all three enter
  the build key, so the verb could not predict a build that used them. Every
  input in the key can now be given to the verb.

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
