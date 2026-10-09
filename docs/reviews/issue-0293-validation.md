# Issue #293 validation: documentation quality pass

## What this changed

Six findings, in order of weight.

### 1. Four planning documents described a migration that had shipped

The shell-to-CPDL conversion completed between 2026-09-18 (1526 `.sh`, 12
`.cbs`) and 2026-10-09 (0 `.sh`, 1010 `.cbs`, 181 distinct packages), measured
in the recipe repository, which retains 1567 historical `.sh` package files and
none live. The documents were accurate when written and the world moved under
them in about eighteen days.

- `integration-blockers.md` blocker 2 is now "The recipe corpus is converted",
  with the measurement, and its External inputs section no longer asks for a
  recipe seed set or migration acceptance criteria. One input remains, the cixd
  test endpoint.
- `roadmap.md` Workstream 5 no longer lists the corpus among absent inputs, and
  Recommended order item 5 is "Integrate cixd".
- `repository-status.md` separates the two claims it had merged: the corpus is
  outside this repository (still true) but is no longer outstanding work.
- `cpdl-corpus-audit.md` is retitled historical and marked closed. Its named
  gaps are resolved in the text, each with how: `toolchain "gcc"` with a
  declared reason for the GCC model, 272 corpus recipes declaring `capability`,
  `require config` for the kernel, and — for the two constructs it judged
  genuinely unsupported — no operation and no approved helper, which was the
  outcome it was written to protect.

`cpdl-test-coverage.md` and `recipes/README.md` carried the same framing and
now say the corpus is converted and lives elsewhere.

**Not asserted:** whether the audit's "134 package revisions" counted packages
or files. `2077/134 = 15.5` matches its stated mean, but the shell corpus held
771+ files then, so 134 cannot be the file count. The figure is left as written
with that ambiguity stated, rather than silently compared against today's 181.

### 2. The cixd guides lacked the feature cixd asked for

#282 added `--events-fd` and `--events-file` for cixd, and
`guides/cixd-embedding.md` — the document a cixd developer is sent to — still
showed only `--events jsonl`. Its process-boundary example now uses
`--events human --events-fd 3`, with why a forking parent wants both and what
happens to a bad descriptor. `library.md` gained the one-sentence version.

### 3. `README.md` led with release mechanics, then re-taught the manual

It now states what CBS is and what a build consists of first, carries a
"where to go" table, and summarises the command surface in a table instead of
re-teaching the validate-and-build walkthrough that the manual and runbook
already own. The feature-completion prose moved out: that is
`repository-status.md`'s job.

The TCC bounds-instrumentation recipe moved to the CI and release guide — and
**it did not work**. It named eight sources, but `src/main.c` has since grown
references into the observation, digest, CIXPKG and plan units, so it failed to
link with five undefined symbols. The guide now derives the list from `SOURCES`
in the `Makefile`, which cannot drift, and the command was run verbatim: 68
parser and validation cases pass instrumented.

### 4. `docs/spec/standalone-cbs-1.0.md` was not a specification

19 non-normative lines in `spec/`, listed under **Specifications**. Moved to
`docs/standalone-scope.md`, retitled as the scope and determinism contract, and
marked non-normative with the specifications named as governing. Its unique
content — the closed list of a build's observable inputs — is kept and
sharpened, because that list is the determinism claim and `make test` asserts
it by requiring byte-identical rebuilds.

### 5. `docs/README.md` was an archive, not a landing page

252 lines, of which ~180 were a flat list of 148 records in non-monotonic
order. Three further defects surfaced while restructuring it:

- a sentence reading "documentation for:" dangled across three interposed
  paragraphs before reaching the list it introduced;
- the ADRs were split 4 in **Architecture decisions** and 33 interleaved in
  **Validation records**, so no complete ADR list existed anywhere;
- **Specifications** listed the user manual, runbook, roadmap, blocker register
  and status file — contradicting the guide/specification distinction the same
  page draws two paragraphs earlier.

Now 83 lines: start-here table, Specifications holding only the two normative
documents, Guides, Project state, and History. The complete indexes are
generated from the filesystem into `docs/adr/README.md` (all 37, ordered) and
`docs/reviews/README.md` (all 148, by issue number, with an explicit note that
a record is history and not status). Verified: every ADR and every record is
reachable, and no document in the tree is orphaned.

### 6. `native-helpers.md` was out of compliance with its own ADR

ADR-0004 requires review "quarterly and at every release". The last entry was
2026-08-28, and fourteen releases had happened since. The review was performed
rather than the gap merely noted: `src/` has no `helper` node kind or parser
keyword, the CPDL grammar has no `helper` production, §10 still excludes it,
and none of the 1010 corpus definitions uses one. Registry remains correctly
empty, and the log now records the search scope and the ADR's cadence.

## Verified, not assumed

- 0 broken internal links across every document, before and after.
- 0 orphans; all 37 ADRs and 148 records reachable.
- Exactly one sentence duplicated verbatim across documents, unchanged.
- The relocated bounds command executed verbatim.
- `make -j1 test` unchanged: 41 PASS, 1 SKIP.

## Deliberately excluded

Clean-host CI, which `integration-blockers.md` blocker 4 and `roadmap.md`
Workstream 4 still nominate. It was declined on 2026-10-09. Their engineering
assessment is left as written rather than rewritten to match a preference; the
decision is recorded in the repository's `CLAUDE.md`.

The `check`/`validate` contradiction found during this pass is #292: it changes
the help string in `src/main.c`, so it is a release rather than a docs edit.
