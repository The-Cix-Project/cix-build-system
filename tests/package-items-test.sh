#!/bin/sh
set -eu

# A package's item set is a contract with several independent consumers: the
# parser, the validator, `explain` and the artifact manifest.  Each keeps its
# own list of what a package carries and nothing checked that they agreed, which
# is how an output's items went missing from `explain` (#313, #314) until
# tests/output-items-test.sh made every omission a written decision.  This is
# the same table for the package's own items (#317).  An output's items are in
# that test and are not repeated here.
#
# Every item the parser accepts at package level needs a row below, stating what
# each consumer does with it.  A cell that says "no" must say why, so an omission
# can only be a decision.  The rows are checked against real behaviour: the item
# is declared in a recipe, built, explained and listed, and a deliberately
# invalid value must be refused.
#
#   item | slot | valid | invalid | explain | manifest | stage mode | why
#
# slot     where the declaration goes, because package items are ordered:
#          version, release and format replace the baseline line, `ident` follows
#          them, `upstream` precedes sources, `sources` replaces the baseline
#          sources, and `tail` follows them.
# explain  text that must appear in `explain --json`, or `-`.
# manifest a whole line that must appear in `cbs list` of the artifact, or `-`.
#          @SHA@ is the source digest and @XSHA@ the digest of the staged file.
# `-` in the explain or manifest column means that consumer does not carry the
# item, and then the last column must say why.  Patterns are fixed strings and
# cannot contain `|`.
cbs=${1:?usage: package-items-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-package-items.XXXXXX")
trap 'rm -rf -- "${root:?}"' EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

cat >"$root/rows" <<'ROWS'
version|version|version "2"|version ""|"version":"2"|identity p-2-1-x86_64||
release|release|release 2|release 0|"release":2|identity p-1-2-x86_64||
format|format|format "cixpkg"|format "tar"|"format":"cixpkg"|-||manifest: the artifact is CIXPKG v2 by construction, so the format is the artifact itself and not a line inside it
license|ident|license "MIT"|license ""|"license":"MIT"|metadata license MIT||
abi|ident|abi "9"|abi ""|"abi":"9"|metadata abi 9||
upstream|upstream|upstream "gitea-tags" { tag "v{version}" source "https://e.test/p-{version}.tar" verify origin }|upstream "nonesuch"|"upstream":{"provider":"gitea-tags"|-||manifest: upstream drives release discovery for the composer and does not describe what was built; no artifact records it
sources|sources|sources { main "m" { url "https://e.test/p-1.tar" url "https://mirror.test/p-1.tar" sha256 "@SHA@" } }|sources { main "m" { url "https://e.test/p-1.tar" sha256 "zz" } }|"urls":["https://e.test/p-1.tar","https://mirror.test/p-1.tar"]|metadata source m https://e.test/p-1.tar @SHA@||
requires|tail|requires { runtime { library "libz" } }|requires { runtime { library "" } }|"requires":{"runtime":{"library":["libz"]}}|-||manifest: requires is explain-only at every level; no artifact records it
replaces|tail|replaces { package "old" }|replaces { package "" }|"replaces":["old"]|-||manifest: replaces is explain-only at every level; no artifact records it
resources|tail|resources { memory "512MiB" }|resources { memory "0" }|"resources":{"memory":536870912}|-||manifest: resources is execution metadata for the orchestrator and does not enter the artifact identity (cpdl-1.0.md); no artifact records it
build_image|tail|build_image "cix-builder"|build_image ""|"build_image":"cix-builder"|-||manifest: build_image is execution metadata consumed by the orchestrator (cpdl-1.0.md); no artifact records it
capability|tail|capability "CAP_SYS_ADMIN"|capability ""|"capabilities":["CAP_SYS_ADMIN"]|-||manifest: capability is execution metadata consumed by the orchestrator (cpdl-1.0.md); no artifact records it
toolchain|tail|toolchain "gcc" { reason "r" }|toolchain "gcc" { reason "" }|"toolchain":"gcc","toolchain_reason":"r"|-||manifest: the toolchain line records the compiler named in requires, not this declaration
tools|tail|requires { build { compiler "tcc" } } tools { compiler alias "cc" }|tools { compiler alias "a/b" }|"tools":[{"kind":"compiler","policy":"alias","source":"cc"}]|metadata tool_policy compiler alias cc||
metadata|tail|metadata { "k" "v" }|metadata { "" "v" }|"metadata":{"k":"v"}|-||manifest: metadata is recipe-author annotation; no artifact records it
privileged|tail|privileged file "${dest}/x" mode 04711|privileged file "${dest}/x" mode 0755|-|f 4711 1 @XSHA@ x|04711|explain: a privileged allowance is not reported by explain; its effect is the staged entry's mode, which the typed manifest retains
ROWS

fail=0
note() {
    echo "package items: $1" >&2
    fail=1
}

# The authority on what a package may carry is the parser.  `any` is the value
# of an `architecture` declaration, which is itself a validation error and is
# checked below; `output` has its own table in output-items-test.sh.
sed -n '/^static CbsNode \*parse_package_item/,/^}/p' src/parser.c |
    grep -oE 'is_word\(parser, "[a-z_]+"\)' | grep -oE '"[a-z_]+"' | tr -d '"' |
    sort -u >"$root/parser-items"
# Failing closed: a refactor of the parser's helpers must not leave this
# comparing nothing and passing.
test "$(wc -l <"$root/parser-items" | tr -d ' ')" -ge 15 ||
    note 'src/parser.c yielded fewer than 15 package items; the extraction no longer matches the parser'
grep -vxE 'any|architecture|output' "$root/parser-items" >"$root/optional-items"
cut -d'|' -f1 "$root/rows" | sort >"$root/row-items"

# Both directions: a new item with no row, and a row for an item that is gone.
comm -23 "$root/optional-items" "$root/row-items" >"$root/unrowed"
while IFS= read -r item; do
    note "the parser accepts \`$item\` at package level but this table has no row for it: decide, for the validator, explain and the manifest, whether each carries it, and say why where it does not"
done <"$root/unrowed"
comm -13 "$root/optional-items" "$root/row-items" >"$root/stale"
while IFS= read -r item; do
    note "this table has a row for \`$item\`, which the parser no longer accepts at package level"
done <"$root/stale"

# One source and one cache entry, shared by every row, so a recipe can be built.
mkdir "$root/payload" "$root/cache"
printf 'x' >"$root/payload/f"
tar -cf "$root/source.tar" -C "$root/payload" .
sha=$(sha256sum "$root/source.tar" | cut -d' ' -f1)
cp "$root/source.tar" "$root/cache/$sha"
xsha=$(printf x | sha256sum | cut -d' ' -f1)

# The `tools` row builds against a compiler the recipe requires.  A stub in a
# directory of its own, put first on the command path, is what
# tests/tool-policy-test.sh does, so the row does not depend on where the host
# keeps tcc.
mkdir "$root/tools"
printf '%s\n' '#!/bin/sh' 'exit 0' >"$root/tools/tcc"
chmod 755 "$root/tools/tcc"

# The recipe for one row: the baseline with the row's text in its slot.
recipe_for() {
    slot=$1 text=$2 mode=$3
    v='version "1"' r='release 1' f='format "cixpkg"' ident='' up=''
    src='sources { main "m" { url "https://e.test/p-1.tar" sha256 "@SHA@" } }'
    tail=''
    case $slot in
        version) v=$text ;;
        release) r=$text ;;
        format) f=$text ;;
        ident) ident=$text ;;
        upstream) up=$text ;;
        sources) src=$text ;;
        tail) tail=$text ;;
    esac
    printf '%s\n' 'package "p" {' "    $v" "    $r" "    $f" "    $ident" \
        "    $up" "    $src" "    $tail" '    build {' \
        "        write \"\${dest}/x\" \"x\" chmod ${mode:-0644}" '    }' '}' |
        sed "s/@SHA@/$sha/g"
}

while IFS='|' read -r item slot valid invalid explain_text manifest_text mode why; do
    dir=$root/$item
    mkdir -p "$dir/ws"
    recipe_for "$slot" "$valid" "$mode" >"$dir/ok.cbs"
    recipe_for "$slot" "$invalid" "$mode" >"$dir/bad.cbs"

    # The validator: a valid declaration is accepted and an invalid one refused.
    if ! "$absolute_cbs" validate "$dir/ok.cbs" >"$dir/ok.out" 2>&1; then
        note "\`$valid\` was refused: $(head -n 1 "$dir/ok.out")"
        continue
    fi
    if "$absolute_cbs" validate "$dir/bad.cbs" >/dev/null 2>&1; then
        note "\`$invalid\` was accepted"
    fi

    # `explain`: the package carries the item, or the row says why.
    "$absolute_cbs" explain "$dir/ok.cbs" --json >"$dir/explain" 2>&1
    if [ "$explain_text" = "-" ]; then
        [ -n "$why" ] ||
            note "\`$item\` is not carried by explain and the row gives no reason"
    else
        grep -qF -- "$explain_text" "$dir/explain" ||
            note "explain does not carry \`$item\` (wanted $explain_text)"
    fi

    # The artifact manifest: the artifact carries the item, or the row says why.
    "$absolute_cbs" build "$dir/ok.cbs" --arch x86_64 --staged "$dir/ws" \
        --output "$dir/p.cixpkg" --cache "$root/cache" \
        --command-path "$root/tools:/usr/bin:/bin" >/dev/null 2>"$dir/build.err" ||
        note "a recipe carrying \`$item\` did not build: $(head -n 1 "$dir/build.err")"
    if [ "$manifest_text" = "-" ]; then
        [ -n "$why" ] ||
            note "\`$item\` is not recorded in the manifest and the row gives no reason"
    elif [ -f "$dir/p.cixpkg" ]; then
        wanted=$(printf '%s' "$manifest_text" | sed "s/@SHA@/$sha/g; s/@XSHA@/$xsha/g")
        "$absolute_cbs" list "$dir/p.cixpkg" | grep -qxF -- "$wanted" ||
            note "the artifact does not record \`$item\` (wanted the line: $wanted)"
    fi
done <"$root/rows"

# `architecture` is the one parser item that is not a row: CBS supplies the
# target from --arch, so declaring it is refused (CPDL-E3006).
mkdir "$root/arch"
printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
    '    format "cixpkg"' '    architecture any' \
    '    build { write "${dest}/x" "x" }' '}' >"$root/arch/a.cbs"
if "$absolute_cbs" validate "$root/arch/a.cbs" >/dev/null 2>"$root/arch/a.err"; then
    note 'an architecture declaration was accepted'
else
    grep -q 'CPDL-E3006' "$root/arch/a.err" ||
        note 'an architecture declaration was refused with a code other than CPDL-E3006'
fi

test "$fail" -eq 0
echo 'package items tests: PASS (every package item has a row, and each consumer matches it)'
