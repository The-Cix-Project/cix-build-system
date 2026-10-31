#!/bin/sh
set -eu

# A documentation example that can be run is run (#309).  A fenced block tagged
#
#     ```cbs check
#
# is extracted and given to `cbs validate`.  Tagging is opt-in: most `cbs`
# blocks are deliberate fragments (`build { ... }`), and a manual that could
# only show runnable examples would be worse for its readers.  An untagged
# block is illustrative and skipped on purpose.
#
# The first measurement of this found three of the four complete examples
# invalid -- the manual's first recipe among them -- all for one missing line.
#
# An extractor is a fresh place for the failure it exists to prevent, so it
# fails closed: it must find something, and a fence it does not understand is an
# error rather than a block that silently opts out.
cbs=${1:?usage: docs-examples-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-docs-examples.XXXXXX")
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

mkdir "$root/blocks"
: >"$root/errors"
: >"$root/index"

# History is not extracted: ADRs and validation records are never rewritten.
find README.md docs -name '*.md' \
    -not -path 'docs/adr/*' -not -path 'docs/reviews/*' | sort >"$root/files"

while IFS= read -r file; do
    awk -v file="$file" -v out="$root/blocks" -v errors="$root/errors" \
        -v index_file="$root/index" '
    # A fence is a line of three backticks, possibly indented inside a list.
    /^[[:space:]]*```/ {
        fence = $0
        indent = match(fence, /[^[:space:]]/) - 1
        info = substr(fence, indent + 4)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", info)
        if (open) {
            open = 0
            if (capture) close(target)
            capture = 0
            next
        }
        open = 1
        capture = 0
        split(info, word, /[[:space:]]+/)
        if (word[1] == "cpdl") {
            printf "%s:%d: fence is ```cpdl; the language is spelled ```cbs\n",
                file, FNR >> errors
        } else if (word[1] == "cbs" && word[2] == "check" && word[3] == "") {
            name = file
            gsub("/", "_", name)
            target = sprintf("%s/%s:%d.cbs", out, name, FNR)
            printf "%s\t%s:%d\n", target, file, FNR >> index_file
            capture = 1
            fence_indent = indent
        } else if (word[1] == "cbs" && word[2] != "") {
            printf "%s:%d: unknown cbs fence tag `%s`; only `check` exists\n",
                file, FNR, word[2] >> errors
        }
        next
    }
    capture {
        line = $0
        if (fence_indent > 0) sub("^[ \t]{0," fence_indent "}", "", line)
        print line > target
    }
    ' "$file"
done <"$root/files"

if [ -s "$root/errors" ]; then
    while IFS= read -r message; do
        echo "docs examples: $message" >&2
    done <"$root/errors"
    exit 1
fi

count=$(ls "$root/blocks" | wc -l | tr -d ' ')
if [ "$count" -eq 0 ]; then
    echo 'docs examples: no ```cbs check block found anywhere; either the' \
        'extractor matched nothing or every example was untagged' >&2
    exit 1
fi

failed=0
while IFS="$(printf '\t')" read -r block where; do
    if ! "$absolute_cbs" validate "$block" >"$root/out" 2>"$root/err"; then
        echo "docs examples: $where does not validate:" >&2
        sed -n '1p' "$root/err" | sed -E "s|^[^ ]+ +||" >&2
        failed=1
    fi
done <"$root/index"
test "$failed" -eq 0

echo "docs examples tests: PASS ($count tagged examples validate)"
