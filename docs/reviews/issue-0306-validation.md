# Issue #306 validation: check the documentation claims that can be checked

Three times in two days a release sweep made the documentation wrong and
nothing noticed. The answer is the one this repository applies to everything
else: a claim that can be checked is checked by the gate, and a claim that
cannot is written so nothing can silently rewrite it.

## The three instances, and why each escaped

1. **The release-note filename.** The sweep rewrote the link to it in
   `docs/README.md`, so the link broke on three consecutive releases and was
   correct only because the file was renamed each time. Nothing checked that a
   relative link resolved.
2. **A measurement sentence.** The release guide said "verified against
   v0.1.124: 71 parser and validation cases pass instrumented". The measurement
   was taken at v0.1.120. The sweep rewrote the version *inside the claim*,
   converting a true statement about one version into an unverified one about
   another — the sweep did exactly what it was told, and the sentence was the
   wrong shape to be swept.
3. **A sweep that matched nothing.** `CLAUDE.md`'s test-group count sat at 45
   against an actual 47, because the wording had drifted and the pattern no
   longer matched. `sed -i` matching nothing exits 0, so a sweep's silence
   means nothing.

An audit then found the same class elsewhere: ADR-0008's implementation note —
which `CLAUDE.md` names as the required home for a new CLI option — had none of
the four added since v0.1.119, and asserted `package` is not exposed when
`cbs --help` has it; `cixd-embedding.md` and `standalone-runbook.md` told a
reader a precomputed fingerprint needed only the inputs that mattered before
the key grew three more; and the ADR and record counts were 37 and 153 against
38 and 162.

## Decision

**Derive, never copy.** `tests/docs-contract-test.sh` computes every expected
value from the filesystem — `ls docs/adr/0*.md`, the record files, the unique
`./tests/…` invocations in the `test:` target, `sed -n 1p VERSION`,
`cbs --help` — and compares the prose against that. A test that read its
expectation from the same prose it checks would pass forever.

Five assertions: the eight swept files each name the release; the ADR, record
and test-group counts match in `CLAUDE.md` and both indexes; every relative
link resolves; ADR-0008's note names every verb in `cbs --help`; every long
option appears in the user manual or the standalone runbook — the two documents
ADR-0008 itself points a reader at, which is why the union is the right check
and the manual alone is not.

**The sweep reports what it changed.** The guide carries a loop printing the
match count per file before editing, and the rule for reading it: `0` means the
reference is missing or reworded; more than expected means the sweep reached a
sentence that is not a current-release reference.

**Measurement sentences carry their version.** "71 cases pass instrumented,
re-run at v0.1.125" is the form, and only re-measuring updates it. An un-run
claim at the current version is worse than an honest one at an older version.

## Rejected

**Generating the documentation from templates.** CBS has no templating, it
would be a second authoring path for prose, and a generated count cannot
disagree with reality — which is precisely the disagreement worth catching.

**Putting the counts in a data file the documents include.** Same objection:
the counts must be *checked against* reality, not *copied from* it.

**Widening the sweep.** The sweep is the hazard, not the remedy.

## Acceptance criteria

`tests/docs-contract-test.sh`, in the gate, fails **twelve** ways against the
tree as of `5b2c68a` — four counts, five unnamed verbs, three undocumented
options — and passes after. It also caught its own addition immediately: adding
it took the gate from 47 groups to 48 and the test failed until `CLAUDE.md`
said 48.

Three options it found undocumented are now documented: `--license`
(`cbs package`'s optional SPDX identifier) in the runbook, and
`--capabilities` and `--help` in the user manual.

Full gate green; all 1983 corpus recipes validate.

## The limit, recorded so a green run is not over-read

This checks counts, links, release references, verbs and option coverage. It
does **not** read prose. "cixd always supplies a finalizer" and "no corpus
recipe reads standard input" can only be re-measured by a person, and the
corpus figures went stale inside a day. A green run means the documentation is
not *provably* wrong, not that it is right.
