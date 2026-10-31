# Issue #308 validation: documentation sweep after v0.1.125

#306 made the documentation claims that can be checked into a gate. This pass is
what a gate cannot do: read the prose against the code, document by document.

## Method

Each document was read and each checkable statement was compared with the
source, the CLI, or a fresh measurement. Claims were not trusted because they
sounded right, and nothing here was written from memory: the environment a
command receives was read from `exec.c`, the provenance keys from `package.c`,
the ordering of `output` from `validate.c`, and every code's category, exit
status and message from the call sites.

Where a statement was about the cix repository's state rather than CBS's, the
cix maintainer was asked, and their answer shaped the fix (below).

## Findings and what was done

**Registry.** 22 of the 24 `CBS-E1xxx` codes appeared in no document, so the
rule that codes are append-only referred to a registry nobody had written down.
Now a table in the manual (section 8.1), and the docs contract test fails if the
source can emit any code that no document lists. That search found #307.

**Authoring.** The manual never explained how to write an `output` block, and
never said what a recipe command receives. Added: section 11.7, and a paragraph
in 11.4 whose contents were read from the source. Also the provenance keys in
section 13, and `output`'s place in the item ordering.

**Guides.** `cixd-embedding.md` gained multi-output at the process boundary, the
descriptor guarantee, the finalizer entry points, and the correct scope of the
ABI number. `ci-and-release.md` stopped describing a CI that does not exist,
gained the corpus check and the ADR-0004 review as part of step 3 (without
renumbering, since other documents cite step numbers), and gained "Closing a
ticket".

**The commands in the guide were run, not trusted.** Both blocks were extracted
from the rendered guide and executed. Doing so found two errors that reading
could not: the corpus snippet changed directory and then called `./cbs`, and
reused one variable for a directory in one block and a file in the next. The
index command reproduces `docs/reviews/README.md` byte for byte.

**Overclaims.** `standalone-scope.md` described a closed list of inputs as
"exactly" six items and said the host's compilers never reach a build; host
executables do run, resolved through the approved path, and their content is in
the key. `integration-blockers.md` said the smoke test needs network access (it
prints SKIP). `roadmap.md` still listed clean-host CI as pending after the owner
declined it.

**cixd's state is not CBS's to document.** The blocker register made claims
about cixd's production readiness. The cix maintainer recommended reducing the
file to what CBS guarantees and pointing at their repository, because such
claims go stale on their release cadence and a CBS document cannot see that. It
was done that way. The same principle is why CLAUDE.md says never to describe
cixd-owned behaviour as a CBS feature.

**A process lapse, found because a document claimed a cadence.** ADR-0004
requires a native-helper review at every release. None was logged for thirteen
releases (v0.1.113–v0.1.125) because the checklist never listed it. One review
was performed with the full scope (source, grammar, the corpus at
`origin/main`, and both trackers) and is logged with the lapse stated rather
than backfilled as thirteen invented entries. The result: no helper request. The
word appears in 65 recipes and every occurrence is ordinary prose.

**Hand-bumps removed.** `docs/library.md` said the ABI "is 6 as of v0.1.124",
true and demanding a bump each release that nothing checked; it now says "since
v0.1.119". `CLAUDE.md` carried the release number in a sentence that already
pointed at `VERSION`; the number is gone.

**Two small repairs to the new gate.** The link check read `[%s](%s)` inside a
fenced code block as a link, so it now skips fenced code. A test that cannot
tolerate documentation quoting link syntax would have gone stale on the first
guide that did.

## Left alone, deliberately

ADRs and validation records (history, and ADR-0029); the substance of the corpus
audit, where only "today" and "now" were pinned to 2026-10-09; and the guide's
filename, which other documents link to. The corpus figures and the
`Residual spec drift (verified …)` heading remain hand-maintained, and are
documented as dated measurements outside the sweep.

## The limit

The gate checks counts, links, release references, verbs, options and codes. It
reads no prose. The corpus figures were quoted four times in one day — 1969,
1983, 2012, 2013 — which is the argument for dating them, not for trusting any
one of them.
