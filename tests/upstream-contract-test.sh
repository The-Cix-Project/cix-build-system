#!/bin/sh
set -eu

cbs=${1:?usage: upstream-contract-test.sh CBS}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cbs-upstream-contract.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cat >"$tmp/token.cbs" <<'EOF'
package "token-upstream" {
    version "1.2.3"
    release 1
    format "cixpkg"
    upstream "gitea-tags" {
        tag "v{version}"
        source "https://osakka:{{REPO_TOKEN}}@git.home.arpa/archive/v{version}.tar.gz"
        verify origin
    }
    sources {
        main "token-upstream" {
            url "https://osakka:{{REPO_TOKEN}}@git.home.arpa/archive/v1.2.3.tar.gz"
            sha256 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        }
    }
    build {
        mkdir "${dest}"
    }
}
EOF

"$cbs" check "$tmp/token.cbs" >/dev/null
"$cbs" explain "$tmp/token.cbs" --json >"$tmp/token.json"
grep -q 'https://osakka:{{REPO_TOKEN}}@git.home.arpa/archive/v{version}.tar.gz' "$tmp/token.json"

sed 's/{{REPO_TOKEN}}/{REPO_TOKEN}/' "$tmp/token.cbs" >"$tmp/bad.cbs"
if "$cbs" check "$tmp/bad.cbs" >/dev/null 2>&1; then
    echo "single-brace token placeholder unexpectedly validated" >&2
    exit 1
fi

cat >"$tmp/checksums.cbs" <<'EOF'
package "checksum-upstream" {
    version "7.2.8"
    release 1
    format "cixpkg"
    upstream "kernel.org" {
        source "https://cdn.kernel.org/pub/linux/kernel/v{major}.x/linux-{version}.tar.xz"
        verify checksums "openpgp-clearsigned" {
            url "https://cdn.kernel.org/pub/linux/kernel/v{major}.x/sha256sums.asc"
            key "B8868C80BA62A1FFFAF5FDA9632D3A06589DA6B1"
        }
    }
    build {
        mkdir "${dest}"
    }
}
EOF
"$cbs" check "$tmp/checksums.cbs" >/dev/null

echo "upstream contract tests: PASS"
