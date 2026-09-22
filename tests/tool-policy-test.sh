#!/bin/sh
set -eu

cbs=${1:?usage: tool-policy-test.sh CBS}
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temporary_dir=${TMPDIR:-/tmp}/cbs-tool-policy-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/tools" "$temporary_dir/staged"
artifact=$temporary_dir/tool-policy.cixpkg

cat >"$temporary_dir/tools/tcc" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" >>args
EOF
chmod 755 "$temporary_dir/tools/tcc"

"$cbs" explain "$tests_dir/fixtures/tool-policy.cbs" --json \
    >"$temporary_dir/explain.json"
grep -q '"name":"tool-policy"' "$temporary_dir/explain.json"

"$cbs" build "$tests_dir/fixtures/tool-policy.cbs" --arch x86_64 \
    --staged "$temporary_dir/staged" \
    --output "$artifact" \
    --command-path "$temporary_dir/tools:/usr/bin:/bin" \
    >"$temporary_dir/build.out" 2>"$temporary_dir/build.err"
test ! -s "$temporary_dir/build.err"
test -x "$temporary_dir/staged/build/.cbs-tools/cc"
test -x "$temporary_dir/staged/build/.cbs-tools/tcc"
grep -qx 'alias-argument' "$temporary_dir/staged/build/args"
grep -qx -- '-MD' "$temporary_dir/staged/build/args"
"$cbs" verify "$artifact" >"$temporary_dir/verify.out"
grep -q 'verified CIXPKG' "$temporary_dir/verify.out"
if grep -qx -- '-MMD' "$temporary_dir/staged/build/args"; then
    echo 'rewrite proxy leaked the original token' >&2
    exit 1
fi

echo 'tool policy test: PASS'
