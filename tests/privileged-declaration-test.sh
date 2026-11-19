#!/bin/sh
set -eu

# A `privileged file` declaration authorizes one staged setuid or setgid file at
# an exact mode.  The specification says its path "must resolve to one file" and
# the staged entry "must match it exactly", but only the converse was enforced:
# a setuid entry with no declaration was refused, while a declaration matching
# nothing was accepted silently (#318).  A stale or mistyped allowance is a
# security statement that no longer describes the artifact, so it fails the
# build and writes no artifact.
cbs=${1:?usage: privileged-declaration-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-privileged-declaration.XXXXXX")
trap 'rm -rf -- "${root:?}"' EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

# build LABEL DECLARATION STAGED-MODE -> builds into $root/LABEL.cixpkg
build() {
    label=$1 declaration=$2 mode=$3
    mkdir "$root/$label-ws"
    printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
        '    format "cixpkg"' "    $declaration" '    build {' \
        '        mkdir "${dest}/usr/bin"' \
        "        write \"\${dest}/usr/bin/s\" \"x\" chmod $mode" '    }' '}' \
        >"$root/$label.cbs"
    "$absolute_cbs" build "$root/$label.cbs" --arch x86_64 \
        --staged "$root/$label-ws" --output "$root/$label.cixpkg" \
        >/dev/null 2>"$root/$label.err"
}

# refused LABEL DECLARATION STAGED-MODE FRAGMENT: must fail, write nothing, and
# name its cause.
refused() {
    label=$1 fragment=$4
    if build "$label" "$2" "$3"; then
        echo "privileged declaration: $label: the build succeeded" >&2
        exit 1
    fi
    [ ! -e "$root/$label.cixpkg" ] ||
        { echo "privileged declaration: $label: an artifact was written" >&2; exit 1; }
    grep -qF -- "$fragment" "$root/$label.err" ||
        { echo "privileged declaration: $label: the refusal does not say '$fragment'" >&2
          cat "$root/$label.err" >&2; exit 1; }
}

# The control: a declaration that matches builds, and the mode is retained.
build match 'privileged file "${dest}/usr/bin/s" mode 04711' 04711 ||
    { echo 'privileged declaration: a matching declaration was refused' >&2
      cat "$root/match.err" >&2; exit 1; }
"$absolute_cbs" list "$root/match.cixpkg" | grep -q '^f 4711 1 .* usr/bin/s$' ||
    { echo 'privileged declaration: the artifact does not retain mode 4711' >&2; exit 1; }

refused missing 'privileged file "${dest}/usr/bin/nope" mode 04711' 0755 \
    'matches no staged path'
refused mismatch 'privileged file "${dest}/usr/bin/s" mode 04711' 0755 \
    'says mode 4711 but the staged file has mode 0755'
refused other-bits 'privileged file "${dest}/usr/bin/s" mode 04711' 04755 \
    'refuses setuid and setgid'
refused directory 'privileged file "${dest}/usr/bin" mode 04711' 0755 \
    'not a regular file'
# The converse, enforced before this: a setuid file with no declaration.
refused undeclared '' 04711 'refuses setuid and setgid'

echo 'privileged declaration tests: PASS (a declaration must match one staged file at its exact mode)'
