#!/bin/sh
set -eu

cbs=${1:?usage: command-path-test.sh CBS}
temporary_dir=${TMPDIR:-/tmp}/cbs-command-path-tests.$$
trap 'rm -rf -- "$temporary_dir"' EXIT HUP INT TERM
mkdir -p -- "$temporary_dir/workspace" "$temporary_dir/tools" 

cat >"$temporary_dir/tools/policy-tool" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod 755 "$temporary_dir/tools/policy-tool"
mkdir -p -- "$temporary_dir/default" "$temporary_dir/rejected" \
    "$temporary_dir/rejected2"

recipe=$temporary_dir/policy.cbs
cat >"$recipe" <<'EOF'
package "command-path" {
    version "1"
    release 1
    format "cixpkg"
    build {
        run "policy-tool" { }
    }
}
EOF

"$cbs" explain "$recipe" >"$temporary_dir/explain.out"
grep -q '^command-path /usr/bin:/bin$' "$temporary_dir/explain.out"
"$cbs" explain "$recipe" --json >"$temporary_dir/explain.json"
grep -q '"command_path":"/usr/bin:/bin"' "$temporary_dir/explain.json"

"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/default" \
    --command-path "$temporary_dir/tools"
test -d "$temporary_dir/default"

set +e
"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/rejected" \
    --command-path ":/usr/bin" >"$temporary_dir/rejected.out" \
    2>"$temporary_dir/rejected.err"
status=$?
set -e
test "$status" -eq 2
grep -q 'must contain only non-empty absolute directories' \
    "$temporary_dir/rejected.err"

set +e
"$cbs" build "$recipe" --arch x86_64 --staged "$temporary_dir/rejected2" \
    --command-path relative >"$temporary_dir/rejected2.out" \
    2>"$temporary_dir/rejected2.err"
status=$?
set -e
test "$status" -eq 2

echo "command path policy test: PASS"
