#!/bin/sh
set -eu

# Documentation claims that can be checked are checked here, so they fail the
# gate instead of going stale quietly.  Three times running, a release sweep
# rewrote a number inside a sentence and nobody noticed: the release-note
# filename, the bounds-check version, and the test-group count whose wording
# had drifted so the sweep silently matched nothing.
cbs=${1:?usage: docs-contract-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-docs-contract.XXXXXX")
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
fail=0
note() {
    echo "docs contract: $1" >&2
    fail=1
}

release=$(sed -n 1p VERSION)

# 1. The release string reaches every file the guide says it must.  A sweep
#    that matches nothing exits 0, so absence has to be an assertion.
for f in README.md docs/README.md docs/repository-status.md docs/roadmap.md \
         docs/user-manual.md docs/integration-blockers.md \
         docs/guides/ci-and-release.md docs/adr/0008-cbs-command-surface.md; do
    grep -qF "$release" "$f" || note "$f does not name release $release"
done

# 2. Counted claims match what is on disk.  Each count is derived here, never
#    copied from the prose it checks.
adrs=$(ls docs/adr/0*.md | wc -l | tr -d ' ')
records=$(ls docs/reviews/*.md | grep -cv 'README\.md$')
groups=$(awk '/^test:/,0' Makefile |
    grep -oE '\./tests/[a-z-]+(-test)?(\.sh)?' | sort -u | wc -l | tr -d ' ')

test "$(sed -n 's/.*All \([0-9]*\) records, in order.*/\1/p' \
    docs/adr/README.md | head -1)" = "$adrs" ||
    note "docs/adr/README.md miscounts the $adrs decisions"
test "$(sed -n 's/.*adr\/ (\([0-9]*\) decisions).*/\1/p' CLAUDE.md | head -1)" \
    = "$adrs" || note "CLAUDE.md layout table miscounts the $adrs decisions"
test "$(sed -n 's/.*all \([0-9]*\) decisions and all.*/\1/p' CLAUDE.md |
    head -1)" = "$adrs" || note "CLAUDE.md miscounts the $adrs decisions"
test "$(sed -n 's/.*all [0-9]* decisions and all \([0-9]*\) validation.*/\1/p' \
    CLAUDE.md | head -1)" = "$records" ||
    note "CLAUDE.md miscounts the $records validation records"
test "$(sed -n 's/.*All \([0-9]*\) records, by issue number.*/\1/p' \
    docs/reviews/README.md | head -1)" = "$records" ||
    note "docs/reviews/README.md miscounts the $records records"
test "$(sed -n 's/.*all \([0-9]*\), one per closed issue.*/\1/p' \
    docs/README.md | head -1)" = "$records" ||
    note "docs/README.md miscounts the $records records"
test "$(sed -n 's/.*the full gate: \([0-9]*\) test groups.*/\1/p' CLAUDE.md |
    head -1)" = "$groups" ||
    note "CLAUDE.md miscounts the $groups test groups"

# 3. Every relative link resolves.  A renamed file is the cheapest way to make
#    documentation lie, and it happened to the release note twice.
find . -name '*.md' -not -path './.git/*' >"$root/md"
while IFS= read -r f; do
    dir=$(dirname "$f")
    grep -oE '\]\([^)#]+\)' "$f" |
        sed -e 's/^](//' -e 's/)$//' |
        grep -vE '^(https?|mailto):' >"$root/links" || true
    while IFS= read -r target; do
        [ -n "$target" ] || continue
        [ -e "$dir/$target" ] || note "$f links to a missing $target"
    done <"$root/links"
done <"$root/md"

# 4. ADR-0008 owns the command surface, so its note names every verb the help
#    text exposes.  Four options reached the CLI before this was noticed.
"$cbs" --help >"$root/help" 2>&1 || true
sed -n 's/^  cbs \([a-z][a-z-]*\).*/\1/p' "$root/help" | sort -u >"$root/verbs"
while IFS= read -r verb; do
    grep -qF "\`$verb\`" docs/adr/0008-cbs-command-surface.md ||
        note "ADR-0008's note does not name the \`$verb\` verb"
done <"$root/verbs"

# 5. Every long option appears in one of the two documents ADR-0008 points a
#    reader at for command examples.
cat docs/user-manual.md docs/standalone-runbook.md >"$root/surface"
grep -oE '\-\-[a-z][a-z-]+' "$root/help" | sort -u >"$root/options"
while IFS= read -r option; do
    grep -qF -- "$option" "$root/surface" ||
        note "$option is in --help but in neither the manual nor the runbook"
done <"$root/options"

test "$fail" -eq 0
echo "docs contract tests: PASS (release refs, counts, links, verbs, options)"
