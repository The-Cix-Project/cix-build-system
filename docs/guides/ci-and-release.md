# CI and release guide

This guide is for maintainers qualifying a CBS checkout and cutting a
release. The commands below assume the repository root is the current working
directory.

## Verify a checkout

Build with the supported compiler and run the default gate:

```sh
make
make test
```

There is no CI for this repository and none is wanted: the gate is run by hand,
on a checkout, by whoever is cutting the release. (The guide's filename is
historical; it stays put for the same reason the release note's does, since
other documents link to it.)

The test target includes parser and diagnostic checks, recipe validation,
package and reproducibility tests, CLI contracts, embedding seams, the
tool-policy and fingerprint-parity tests, the descriptor and finalizer-identity
contracts, the multi-output partition, and the documentation contract test. It
intentionally skips the upstream zstd smoke test when no source cache is
configured. Run that qualification explicitly when the digest-keyed cache is
available:

```sh
make upstream-test CBS_UPSTREAM_CACHE=/path/to/source-cache
```

The cache must contain each source archive under its declared SHA-256 digest.
That keeps the gate's network policy explicit — `make test` itself never
downloads anything — and makes a successful qualification replayable.

## Bounds instrumentation

TCC can instrument the parser and executor with bounds checking, which the
default build does not enable. Run the parser and validation suite against the
instrumented binary when changing lexer, parser, validator, or filesystem
code:

```sh
tcc -b -Isrc -DCBS_VERSION='"'"$(sed -n 1p VERSION)"'"' \
    -std=c11 -Wall -Wextra -Werror -pedantic \
    $(sed -n '/^SOURCES :=/,/^$/p' Makefile | grep -oE 'src/[a-z]+\.c') \
    -larchive -lzstd -ldl -o /tmp/cbs-bounds
./tests/parser-validation.sh /tmp/cbs-bounds
```

Build every source, not a subset: `src/main.c` reaches into the observation,
digest, CIXPKG and plan units, so a hand-picked list stops linking the moment
the CLI grows a reference. Taking the list from `SOURCES` in the `Makefile`
keeps it correct without maintaining a second copy.

A bounds violation aborts with a TCC diagnostic rather than failing an
assertion, so treat any abort as a defect in the code under test. Re-run it
each release and record the result, with the version it was run at, in
`docs/release-notes.md` under Verification. It is recorded there and not here
because this file is in the release sweep: a measurement written in a swept file
is rewritten to the new version whether or not anyone re-ran it.

Useful focused checks while developing are:

```sh
./cbs check recipe.cbs
./cbs explain recipe.cbs --json
./cbs doctor recipe.cbs --arch x86_64 --staged /tmp/cbs-doctor
./tests/cli-contract-test.sh ./cbs
./tests/tool-policy-test.sh ./cbs
```

## Check documentation and release state

Before tagging, confirm that `VERSION`, the current-release statements, and
the intended tag agree:

```sh
sed -n '1p' VERSION
git status --short --branch
git diff --check
```

A release build invokes `version-check`; a checkout at tag `vX.Y.Z` must have
`X.Y.Z` as the first line of `VERSION`. Keep the executable, static archive,
public header, and pkg-config metadata from the same tag.

## Release checklist

1. Update implementation, tests, guides, and current release references.
   The consolidated release note is `docs/release-notes.md` — a **stable
   path**, extended in place. Do not put a version in its filename and do not
   name it in the reference sweep: the sweep rewrote the link to it in
   `docs/README.md` and broke that link on three consecutive releases, and a
   version in the link text would advance the span it claims to cover whether
   or not the note was extended. Its `# ` heading is the one place the covered
   span lives. Cix cites this path from its own append-only ADRs, where a
   stale link cannot be quietly fixed.

   Run the reference sweep so it **reports what it changed**. A bare `sed -i`
   that matches nothing exits 0, which is how `CLAUDE.md`'s test-group count
   drifted for three releases once its wording stopped matching the pattern;
   and a `sed` that matches a line it had no business touching is how this
   guide came to claim a bounds-check measurement at a version it had never
   been run at. Count per file, and read the output:

   ```sh
   previous=$(sed -n 1p VERSION)   # capture before editing VERSION
   for f in README.md docs/README.md docs/repository-status.md \
            docs/roadmap.md docs/user-manual.md \
            docs/integration-blockers.md docs/guides/ci-and-release.md \
            docs/adr/0008-cbs-command-surface.md; do
       printf '%s: %s\n' "$f" "$(grep -c "$previous" "$f" || true)"
       sed -i "s/$previous/$NEW/g" "$f"
   done
   ```

   A file reporting `0` means the sweep did nothing there, so the reference is
   missing or reworded. A file reporting more occurrences than expected means
   the sweep reached a sentence that is not a current-release reference.

   Some statements carry a version or a date because they are *measurements*,
   and are deliberately outside the sweep: the `Residual spec drift (verified
   …)` heading in `CLAUDE.md`, the corpus figures in `CLAUDE.md` and the release
   note, and the bounds-check result in the release note. Re-measure them and
   change the version only when you have. `docs/library.md` states the ABI as
   "6, since v0.1.119" for the same reason: a statement true of every release
   since a point needs no bump.

   **A sentence reporting a measurement states the version it was measured
   at, and only re-measuring updates it.** "71 cases pass instrumented, re-run
   at v0.1.128" is the form. Never let the sweep advance that version: re-run
   the measurement, or leave the older version standing. An un-run claim at
   the current version is worse than an honest one at an older version.

   `tests/docs-contract-test.sh` enforces what can be compared mechanically:
   that all eight files name the release, that the ADR, record and test-group
   counts match the filesystem, that every relative link resolves, that
   ADR-0008's note names every verb `cbs --help` exposes, that every long option
   appears in the manual or the runbook, that every diagnostic code the source
   can emit is listed in a document, that the newest entry in the
   native-helper review log names this release, and that every keyword the
   specification reserves is shown as code in the manual. The last is how
   absences are found: `patch`, `truncate`, `links`, `nonempty` and the
   `checksums` and `signature` verify forms were all implemented, several used
   by the corpus, and absent from the manual until it existed. A bare word does
   not count, since the English "links against" once made the `links` operation
   look documented.

   `tests/docs-examples-test.sh` checks the examples. A fenced block whose info
   string is `cbs check` is extracted and given to `cbs validate`, which parses
   and validates a recipe and executes nothing; use that info string for any
   example that is a complete recipe. Validating is not building: the manual's
   first recipe was also built by hand (it has no sources, so it can be) and
   builds, but no other tagged example is buildable, so the gate does not claim
   to build any. Fragments (`build { ... }`) keep the bare info string `cbs` and
   are illustrative by design, since a manual that could only show complete
   recipes would be worse to read. The test fails closed: finding no tagged
   block is a failure, and so is an unknown tag or the `cpdl` spelling, because
   a typo must not silently opt a block out. Before it existed, three of the
   four complete examples in the manual and the specification were invalid, the
   manual's first recipe among them, for one missing line.

   Neither reads prose, so a green run does not mean the documentation is
   correct, only that what could be checked is not wrong. What is left is
   absences (a section that was never written) and interpretation.
2. Run `git diff --check`, then `make -o version-check test`. At a tagged
   checkout `version-check` refuses the bumped `VERSION` until the commit in
   step 4 moves `HEAD` off the previous tag, so the full `make -j1 test` runs
   after that commit and again from the new tag in step 7.
3. Confirm the upstream qualification result is either passed or explicitly
   skipped because `CBS_UPSTREAM_CACHE` is unset. Also validate the recipe
   corpus, which is far broader than the one fixture shipped here, and run the
   native-helper review that ADR-0004 requires at every release; the gate fails
   if the review log's newest entry does not name the release.

   Measure the corpus at `origin/main`, not in the working tree of
   `/home/osakka/cix-recipes`. cixd commits there, so the checkout can sit
   behind and a `git pull` can race a write-back. Fetch and extract instead,
   which is read-only and exact:

   ```sh
   cbs=$PWD/cbs corpus=$(mktemp -d) &&
   git -C /home/osakka/cix-recipes fetch -q origin &&
   git -C /home/osakka/cix-recipes archive origin/main recipes |
       tar -x -C "$corpus" &&
   (cd "$corpus" && for r in $(find recipes -name '*.cbs'); do
       "$cbs" validate "$r" >/dev/null || echo "FAIL $r"; done)
   rm -rf "${corpus:?}"
   ```

   Record the definition count, the package count and the commit they were
   measured at, and treat last release's figure as already stale — it has moved
   twice in a day. The helper review searches `src/`, the grammar, the corpus
   and both trackers for a native helper, per `docs/native-helpers.md`; add a
   dated entry to its review log even when nothing matched.
4. Commit the change with a focused message.
5. Create the matching annotated tag, for example `git tag -a v0.1.128 -m
   "CBS v0.1.128"`.
6. Push the branch and tag together.
7. Re-run `make -j1 test` from the tagged checkout.
8. Update the issue tracker with the commit, tag, test result, and any
   intentional qualification skip.

## Closing a ticket

Every fix ships with `docs/reviews/issue-NNNN-validation.md`, whose first line
is `# Issue #NNN validation: ` followed by a short title, and the reviews index
is regenerated from the files rather than edited by hand:

```sh
index=$(mktemp) && {
  sed '/^All [0-9]* records, by issue number:/,$d' docs/reviews/README.md
  printf 'All %s records, by issue number:\n\n' \
      "$(ls docs/reviews/issue-*-validation.md | wc -l | tr -d ' ')"
  for f in $(ls docs/reviews/issue-*-validation.md | sort -t- -k2 -n); do
      printf -- '- [%s](%s)\n' "$(sed -n '1s/^# //p' "$f")" "$(basename "$f")"
  done
} > "$index" && mv "$index" docs/reviews/README.md
```

Then update the record count in `docs/README.md` and `CLAUDE.md`. If you forget,
`tests/docs-contract-test.sh` fails and names the file. The commit says
`Resolves #N`; the tracker gets a comment with before and after evidence, and
the ticket is closed.

Do not describe cixd-owned sandboxing, image composition, repository
publication, signing, or transaction behavior as a CBS release feature. Those
boundaries are defined in the integration contract.
