# Native helper registry

This is the authoritative registry required by ADR-0004. An empty registry is
the correct state until a helper has an approved exception record.

| Helper | Package | Approval issue | Commit | Owner | Scope | Tests | Last review |
| --- | --- | --- | --- | --- | --- | --- | --- |
| None | — | — | — | — | No package-specific native helper is currently approved. | — | 2026-10-09 |

## Review log

ADR-0004 requires a review quarterly and at every release. Each entry records
its date, the search scope, what matched, and the decision.

- 2026-10-09 (v0.1.112): searched `src/` for a `helper` node kind or parser
  keyword, the CPDL 1.0 grammar for a `helper` production, and the converted
  recipe corpus (1010 definitions) for a `helper` operation. No matches in any
  of the three. The two constructs the
  [corpus audit](cpdl-corpus-audit.md) judged genuinely unsupported — general
  `find`/`-exec` and ambient mtime manipulation — were answered by explicit
  recipe operations and the build systems' own facilities, so neither produced
  a helper request. §10 of the specification still lists the native `helper`
  operation as excluded. Registry remains empty; no generalization issue is
  required.
- 2026-08-28: initial registry created; searched the repository and current
  issue set. No existing native helper implementation or approved exception
  was found. No follow-up CPDL generalization issue is required yet.
