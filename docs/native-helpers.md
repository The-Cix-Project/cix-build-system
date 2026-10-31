# Native helper registry

This is the authoritative registry required by ADR-0004. An empty registry is
the correct state until a helper has an approved exception record.

| Helper | Package | Approval issue | Commit | Owner | Scope | Tests | Last review |
| --- | --- | --- | --- | --- | --- | --- | --- |
| None | — | — | — | — | No package-specific native helper is currently approved. | — | 2026-10-31 |

## Review log

ADR-0004 requires a review quarterly and at every release. Each entry records
its date, the search scope, what matched, and the decision.

- 2026-10-31 (v0.1.127): the same four searches as the entry below, re-run for
  this release because the documentation gate now fails without an entry naming
  it. `src/`: no `helper` node kind or keyword (0). Grammar: no `helper`
  production (0). The recipe corpus at `origin/main` `802e05c`, 2016 definitions
  across 246 packages: 0 with a `helper` operation; the word still appears in 65
  recipes, as ordinary prose. Trackers: #142 and #151 as assessed below, plus
  #308 and #309, which are this repository's own tickets *about* this review
  process and not requests for a helper. No cix issue matches. Decision:
  registry remains empty.
- 2026-10-31 (v0.1.126): **a lapse, then a review.** ADR-0004 requires a
  review at every release, and none was logged for the thirteen releases from
  v0.1.113 to v0.1.125: the release checklist did not list it, so it was not
  run. This entry covers that interval in one pass rather than inventing
  thirteen after-the-fact entries; the checklist now carries it. Scope and
  matches:
  - `src/`: no `helper` node kind or parser keyword (0 matches).
  - The CPDL 1.0 grammar: no `helper` production (0).
  - The recipe corpus, measured at `origin/main` `d1c6110`: 2012 definitions
    across 246 packages, **0** with a `helper` operation. The word appears in
    65 recipes, and every occurrence is ordinary prose — a cixd "helper child"
    process, a libgcc "TImode helper" compiler runtime function, an
    `LD_PRELOAD` buffering helper built inside a test — none a request for a
    CBS native helper.
  - Closed CBS issues matching "native helper": #142, the corpus audit
    already recorded here, and #151, which asked for a *general* CPDL facility
    (a list-valued `write`) on behalf of exactly one package, the kernel. One
    request from one package is below ADR-0004's two-request threshold, and it
    asked for a language operation rather than a helper, which is the outcome
    the ADR prefers. No open CBS issue and no cix issue matches.
  - Decision: registry remains empty; no generalization issue is required.
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
