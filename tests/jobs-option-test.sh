#!/bin/sh
set -eu

# `${jobs}` was 1 on every host because the build hard-coded it, so every recipe
# that passes "-j${jobs}" built single-threaded however many CPUs it had (#320).
# It is now what the caller asks for with --jobs, else what the CPU budget
# allows.  The budget itself depends on the host, so what is asserted about the
# default is that it is a positive integer and is the number `doctor` reports;
# the detection arithmetic is in tests/jobs-test.c, against a faked cgroup tree.
#
# `${jobs}` is policy and not an input: no value derived from the host may reach
# the build fingerprint, or one recipe would key differently on different
# machines.  That is asserted here by building at two values and comparing.
cbs=${1:?usage: jobs-option-test.sh CBS}
root=$(mktemp -d "${TMPDIR:-/tmp}/cbs-jobs-option.XXXXXX")
trap 'rm -rf -- "${root:?}"' EXIT HUP INT TERM
case $cbs in
    /*) absolute_cbs=$cbs ;;
    *) absolute_cbs=$(CDPATH= cd -- "$(dirname -- "$cbs")" && pwd)/$(basename -- "$cbs") ;;
esac

# A recipe that records what `${jobs}` was, and one that only passes it to a
# real command, as the kernel recipe does with make.
printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
    '    format "cixpkg"' '    build { write "${dest}/jobs" "${jobs}\n" }' '}' \
    >"$root/record.cbs"
printf '%s\n' 'package "p" {' '    version "1"' '    release 1' \
    '    format "cixpkg"' '    build { run "echo" { "-j${jobs}" jobs $jobs } }' '}' \
    >"$root/pass.cbs"

# built LABEL RECIPE [OPTION...] -> prints what ${jobs} was, from the artifact
built() {
    label=$1 recipe=$2
    shift 2
    mkdir "$root/$label-ws"
    "$absolute_cbs" build "$root/$recipe.cbs" --arch x86_64 \
        --staged "$root/$label-ws" --output "$root/$label.cixpkg" "$@" \
        >/dev/null 2>"$root/$label.err"
}
recorded() { tr -d '\n' <"$root/$1-ws/dest/jobs"; }

fail() {
    echo "jobs option: $1" >&2
    exit 1
}

# The override, in both spellings and in the last position, where an option
# the build parser does not assign would fall into its --events catch-all.
built three record --jobs 3 || fail '--jobs 3 was refused'
[ "$(recorded three)" = 3 ] || fail "--jobs 3 gave $(recorded three)"
built five record --jobs=5 || fail '--jobs=5 was refused'
[ "$(recorded five)" = 5 ] || fail "--jobs=5 gave $(recorded five)"
built last record --events jsonl --jobs 7 || fail '--jobs after --events was refused'
[ "$(recorded last)" = 7 ] || fail "--jobs 7 after --events gave $(recorded last)"
# An explicit value is used as given, not capped by what this host could run,
# because the caller knows what the build's sandbox allows.
built big record --jobs 4096 || fail '--jobs 4096 was refused'
[ "$(recorded big)" = 4096 ] || fail "--jobs 4096 gave $(recorded big)"

# Anything that is not a positive integer is a usage error, exit 2, and builds
# nothing.
# strtol alone would take the last three: it saturates, and accepts a sign and
# leading space.
for bad in 0 -2 abc 1.5 3x +3 99999999999999999999 ' 3'; do
    mkdir "$root/bad-$bad-ws"
    status=0
    "$absolute_cbs" build "$root/record.cbs" --arch x86_64 \
        --staged "$root/bad-$bad-ws" --output "$root/bad-$bad.cixpkg" \
        --jobs "$bad" >/dev/null 2>"$root/bad-$bad.err" || status=$?
    [ "$status" -eq 2 ] || fail "--jobs $bad exited $status, expected 2"
    grep -q 'CBS-E1025' "$root/bad-$bad.err" ||
        fail "--jobs $bad was not refused with CBS-E1025"
    [ ! -e "$root/bad-$bad.cixpkg" ] || fail "--jobs $bad wrote an artifact"
done

# The default is a positive integer and is what doctor says it will be.
built auto record || fail 'a build without --jobs failed'
auto=$(recorded auto)
case $auto in
    '' | *[!0-9]* | 0) fail "the default \${jobs} is not a positive integer: '$auto'" ;;
esac
"$absolute_cbs" doctor "$root/record.cbs" --arch x86_64 --staged "$root" \
    >"$root/doctor.out" 2>&1 || true
reported=$(sed -n 's/^doctor: PASS jobs: ${jobs} defaults to \([0-9][0-9]*\) from .*/\1/p' "$root/doctor.out")
[ "$reported" = "$auto" ] ||
    fail "doctor reports '$reported' but the build used '$auto'"

# Nothing derived from the host may key the build.  Two values, the default and
# `cbs fingerprint`, which has no --jobs at all, must all agree.
built one pass --jobs 1 || fail 'a build passing -j${jobs} to a command failed'
built four pass --jobs 4 || fail 'a build passing -j${jobs} to a command failed'
fingerprint_of() { "$absolute_cbs" list "$root/$1.cixpkg" | sed -n 's/^metadata build_fingerprint //p'; }
[ -n "$(fingerprint_of one)" ] || fail 'the artifact records no build fingerprint'
[ "$(fingerprint_of one)" = "$(fingerprint_of four)" ] ||
    fail 'the build fingerprint changed with --jobs'
[ "$(fingerprint_of one)" = "$("$absolute_cbs" fingerprint "$root/pass.cbs" --arch x86_64 | sed -n 's/^fingerprint //p')" ] ||
    fail 'cbs fingerprint disagrees with the build'

echo 'jobs option tests: PASS (--jobs overrides, bad values refused, the default is what doctor reports, the fingerprint ignores it)'
