#!/bin/sh
set -eu

cbs=${1:?usage: stdout-test.sh CBS}
root=${TMPDIR:-/tmp}/cbs-stdout-test.$$
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p -- "$root/stage"
cat >"$root/recipe.cbs" <<'EOF'
package "stdout-test" {
    version "1"
    release 1
    format "cixpkg"
    build {
        run "printf" {
            "  hello world  \n"
            expect { stdout contains "world" }
            stdout "VALUE"
        }
        run "printf" {
            "${stdout.VALUE}\n"
            expect { stdout contains "hello world" }
        }
    }
}
EOF
"$cbs" build "$root/recipe.cbs" --arch x86_64 --staged "$root/stage" \
    >"$root/out"
grep -q '^staged ' "$root/out"
printf '%s\n' 'stdout tests: PASS (bounded assertion and binding)'
