#!/bin/sh
set -eu

# An output's item set is a contract with several independent consumers: the
# parser, the validator, `explain` and the artifact manifest.  Each kept its own
# list of what an output carries, and nothing checked that they agreed, so the
# same defect recurred.  #313: the validator never visited an output's own
# license, requires and replaces.  #314: `explain` dropped an output's replaces.
# The abi work (#312) had to touch every consumer by hand, and it was remembered
# only because it was enumerated.
#
# Every item the parser accepts inside an output needs a row below, stating what
# each consumer does with it.  A cell that says "no" must say why, so an omission
# can only be a decision.  The rows are checked against real behaviour, not
# against the source text: the item is declared in a recipe, built, explained and
# listed, and a deliberately invalid value must be refused.
#
#   item | valid declaration | explain pattern | manifest pattern | invalid | why
#
# `-` in the explain or manifest column means that consumer does not carry the
# item, and then the last column must say why.  Patterns are basic regular
# expressions and cannot contain `|`.
cbs=${1:?usage: output-items-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-output-items.XXXXXX")
trap 'rm -rf -- "${root:?}"' EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

cat >"$root/rows" <<'ROWS'
abi|abi "9"|"abi":"9"}|^metadata abi 9$|abi ""|
license|license "MIT"|-|^metadata license MIT$|license ""|explain: an output's license is not reported in outputs[]; cixd reads no license from explain at any level, the main package's included (cix maintainer, 2026-11-16); if licences are ever shown it will be a cix feature at package and output level together
replaces|replaces { package "old" }|"replaces":\["old"\]|-|replaces { package "" }|manifest: replaces is explain-only at every level; no artifact records it
requires|requires { runtime { library "libz" } }|"requires":{"runtime":{"library":\["libz"\]}}|-|requires { runtime { library "" } }|manifest: requires is explain-only at every level; no artifact records it
ROWS

fail=0
note() {
    echo "output items: $1" >&2
    fail=1
}

# The authority on what an output may carry is the parser.  `files` is the
# structural member every output needs and `glob` modifies a selector, so
# neither is an optional item with consumers of its own.
sed -n '/^static CbsNode \*parse_output/,/^}/p' src/parser.c |
    grep -oE 'is_word\(parser, "[a-z]+"\)' | grep -oE '"[a-z]+"' | tr -d '"' |
    sort -u >"$root/parser-items"
# Failing closed: a refactor of the parser's helpers must not leave this
# comparing nothing and passing.
test "$(wc -l <"$root/parser-items" | tr -d ' ')" -ge 5 ||
    note 'src/parser.c yielded fewer than 5 output items; the extraction no longer matches the parser'
grep -vxE 'files|glob' "$root/parser-items" >"$root/optional-items"
cut -d'|' -f1 "$root/rows" | sort >"$root/row-items"

# Both directions: a new item with no row, and a row for an item that is gone.
comm -23 "$root/optional-items" "$root/row-items" >"$root/unrowed"
while IFS= read -r item; do
    note "the parser accepts \`$item\` inside an output but this table has no row for it: decide, for the validator, explain and the manifest, whether each carries it, and say why where it does not"
done <"$root/unrowed"
comm -13 "$root/optional-items" "$root/row-items" >"$root/stale"
while IFS= read -r item; do
    note "this table has a row for \`$item\`, which the parser no longer accepts in an output"
done <"$root/stale"

while IFS='|' read -r item valid explain_pattern manifest_pattern invalid why; do
    dir=$root/$item
    mkdir -p "$dir/ws"
    recipe_for() {
        printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
            '    format "cixpkg"' '    output "x" {' \
            '        files { "usr/bin/x" }' "        $1" '    }' '    build {' \
            '        mkdir "${dest}/usr/bin"' \
            '        write "${dest}/usr/bin/x" "x" chmod 0755' \
            '        write "${dest}/usr/bin/y" "y" chmod 0755' '    }' '}'
    }
    recipe_for "$valid" >"$dir/ok.cbs"
    recipe_for "$invalid" >"$dir/bad.cbs"

    # The validator: a valid declaration is accepted and an invalid one refused.
    if ! "$absolute_cbs" validate "$dir/ok.cbs" >"$dir/ok.out" 2>&1; then
        note "\`$valid\` in an output was refused: $(head -n 1 "$dir/ok.out")"
        continue
    fi
    if "$absolute_cbs" validate "$dir/bad.cbs" >/dev/null 2>&1; then
        note "\`$invalid\` in an output was accepted, though the same item is refused on the package"
    fi

    # `explain`: the entry for the output carries the item, or the row says why.
    "$absolute_cbs" explain "$dir/ok.cbs" --json >"$dir/explain" 2>&1
    if [ "$explain_pattern" = "-" ]; then
        [ -n "$why" ] ||
            note "\`$item\` is not carried by explain and the row gives no reason"
    else
        grep -q "\"name\":\"x\",\"main\":false,.*$explain_pattern" "$dir/explain" ||
            note "explain does not carry \`$item\` on the output's own entry (wanted $explain_pattern)"
    fi

    # The artifact manifest: the output's own artifact carries the item, or the
    # row says why.
    "$absolute_cbs" build "$dir/ok.cbs" --arch x86_64 --staged "$dir/ws" \
        --output-dir "$dir" >/dev/null 2>"$dir/build.err" ||
        note "a recipe carrying \`$item\` in an output did not build: $(head -n 1 "$dir/build.err")"
    if [ "$manifest_pattern" = "-" ]; then
        [ -n "$why" ] ||
            note "\`$item\` is not recorded in the manifest and the row gives no reason"
    elif [ -f "$dir/x-1-1-x86_64.cixpkg" ]; then
        "$absolute_cbs" list "$dir/x-1-1-x86_64.cixpkg" |
            grep -q "$manifest_pattern" ||
            note "the output's artifact does not record \`$item\` (wanted $manifest_pattern)"
    fi
done <"$root/rows"

test "$fail" -eq 0
echo 'output items tests: PASS (every output item has a row, and each consumer matches it)'
