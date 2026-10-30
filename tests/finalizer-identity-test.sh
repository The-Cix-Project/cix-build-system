#!/bin/sh
set -eu

# The fingerprint binds what the finalizer IS, not that one ran (#303).
cbs=${1:?usage: finalizer-identity-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-finalizer-identity.XXXXXX")
trap 'chmod -R u+w -- "$root" 2>/dev/null || true; rm -rf -- "$root"' \
    EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac
recipe=tests/fixtures/standalone-smoke.cbs

# Two finalizers at the SAME path, differing only in content: a composer's
# finalizer path is typically fixed while its content changes per release, so
# a path-bound key would give every one of them the same identity.
mkdir "$root/bin" "$root/one" "$root/two"
printf '#!/bin/sh\nexit 0\n' >"$root/bin/finalize"
chmod 755 "$root/bin/finalize"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/one" \
    --output "$root/one.cixpkg" --finalize-command "$root/bin/finalize" \
    >/dev/null
printf '#!/bin/sh\n: changed\nexit 0\n' >"$root/bin/finalize"
chmod 755 "$root/bin/finalize"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/two" \
    --output "$root/two.cixpkg" --finalize-command "$root/bin/finalize" \
    >/dev/null

key_of() {
    "$absolute_cbs" list "$1" | sed -n 's/^metadata build_fingerprint //p'
}
one=$(key_of "$root/one.cixpkg")
two=$(key_of "$root/two.cixpkg")
printf '%s\n' "$one" | grep -Eq '^[0-9a-f]{64}$'
printf '%s\n' "$two" | grep -Eq '^[0-9a-f]{64}$'
if [ "$one" = "$two" ]; then
    echo 'finalizer identity tests: two finalizers gave one key' >&2
    exit 1
fi

# The artifact says what finalized it, and it is the digest of that file.
recorded=$("$absolute_cbs" list "$root/two.cixpkg" |
    sed -n 's/^metadata finalizer //p')
test "$recorded" = "$(sha256sum "$root/bin/finalize" | cut -d' ' -f1)"

# The verb predicts a finalizer build, which it could not before: parity is
# what makes a key usable for a cache lookup before building.
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        --finalize-command "$root/bin/finalize")" = "fingerprint $two"

# The space form and the = form agree, so the option is not being swallowed by
# the parser's catch-all.
test "$("$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
        "--finalize-command=$root/bin/finalize")" = "fingerprint $two"

# A finalizer the approved command path cannot reach is a usage refusal, in
# both the verb and the build.
if "$absolute_cbs" fingerprint "$recipe" --arch x86_64 \
    --finalize-command "$root/bin/absent" >"$root/o" 2>"$root/e"; then
    echo 'finalizer identity tests: an unreachable finalizer was accepted' >&2
    exit 1
fi
grep -F 'CBS-E1006' "$root/e" >/dev/null

# No finalizer: no identity recorded, and the key is still available.
mkdir "$root/none"
"$absolute_cbs" build "$recipe" --arch x86_64 --staged "$root/none" \
    --output "$root/none.cixpkg" >/dev/null
key_of "$root/none.cixpkg" | grep -Eq '^[0-9a-f]{64}$'
if "$absolute_cbs" list "$root/none.cixpkg" |
        grep -q '^metadata finalizer '; then
    echo 'finalizer identity tests: recorded a finalizer for none' >&2
    exit 1
fi

echo 'finalizer identity tests: PASS (content-bound, recorded, verb parity)'
