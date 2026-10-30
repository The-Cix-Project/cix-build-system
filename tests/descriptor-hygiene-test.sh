#!/bin/sh
set -eu

# A recipe command's descriptor table is constructed by CBS, not inherited.
cbs=${1:?usage: descriptor-hygiene-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-descriptor-hygiene.XXXXXX")
trap 'chmod -R u+w -- "$root" 2>/dev/null || true; rm -rf -- "$root"' \
    EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

mkdir "$root/stage"
cat >"$root/probe.cbs" <<'RECIPE'
package "descriptor-probe" {
    version "1"
    release 1
    format "cixpkg"
    build {
        run "ls" { "-l" "/proc/self/fd/" }
        write "${dest}/probe" "ok\n"
    }
}
RECIPE

# Hand CBS an events file and leak two descriptors into it from this shell,
# standing in for a composer that does the same -- cixd passes --events-file
# and CBS is itself run as a recipe command when CBS builds CBS.
exec 8>"$root/leaked-eight"
exec 9>"$root/leaked-nine"
"$absolute_cbs" build "$root/probe.cbs" \
    --arch x86_64 --staged "$root/stage" --output "$root/probe.cixpkg" \
    --events-file "$root/events.jsonl" --events human \
    >"$root/probe-output" 2>&1
exec 8>&-
exec 9>&-

# `ls` opens one descriptor of its own for the directory it is listing, so the
# child may hold 0, 1, 2 and that one.  Nothing else.
sed -n 's/^l[-r][-w][-x].* \([0-9][0-9]*\) ->.*/\1/p' "$root/probe-output" \
    >"$root/descriptors"
test -s "$root/descriptors"
if awk '$1 > 3 { found = 1 } END { exit !found }' "$root/descriptors"; then
    echo 'descriptor hygiene tests: a command inherited a descriptor' >&2
    sed -n '/\/proc\/self\/fd/,$p' "$root/probe-output" >&2
    exit 1
fi

# Name the two that matter, so a regression says which: the event stream the
# composer trusts, and a descriptor the composer never meant to share.
for leaked in events.jsonl leaked-eight leaked-nine; do
    if grep -q -- "$leaked" "$root/probe-output"; then
        echo "descriptor hygiene tests: a command inherited $leaked" >&2
        exit 1
    fi
done

# The build still works and the events still arrive: the table is constructed,
# not merely emptied.
"$absolute_cbs" verify "$root/probe.cixpkg" >/dev/null
grep -F '"type":"build-end"' "$root/events.jsonl" >/dev/null
test ! -s "$root/leaked-eight"
test ! -s "$root/leaked-nine"

echo 'descriptor hygiene tests: PASS (no descriptor above stderr is inherited)'
