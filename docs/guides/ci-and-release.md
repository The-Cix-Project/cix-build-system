# CI and release guide

This guide is for maintainers qualifying a CBS checkout and cutting a
release. The commands below assume the repository root is the current working
directory.

## Verify a checkout

Build with the supported compiler and run the default gate:

```sh
make
make test
```

The test target includes parser and diagnostic checks, recipe validation,
package/reproducibility tests, CLI contracts, embedding seams, and the
tool-policy parity test. It intentionally skips the upstream zstd smoke test
when no source cache is configured. Run that qualification explicitly when
the digest-keyed cache is available:

```sh
make upstream-test CBS_UPSTREAM_CACHE=/path/to/source-cache
```

The cache must contain each source archive under its declared SHA-256 digest.
This keeps CI network policy explicit and makes a successful qualification
replayable.

## Bounds instrumentation

TCC can instrument the parser and executor with bounds checking, which the
default build does not enable. Run the parser and validation suite against the
instrumented binary when changing lexer, parser, validator, or filesystem
code:

```sh
tcc -b -Isrc -DCBS_VERSION='"'"$(sed -n 1p VERSION)"'"' \
    -std=c11 -Wall -Wextra -Werror -pedantic \
    $(sed -n '/^SOURCES :=/,/^$/p' Makefile | grep -oE 'src/[a-z]+\.c') \
    -larchive -lzstd -ldl -o /tmp/cbs-bounds
./tests/parser-validation.sh /tmp/cbs-bounds
```

Build every source, not a subset: `src/main.c` reaches into the observation,
digest, CIXPKG and plan units, so a hand-picked list stops linking the moment
the CLI grows a reference. Taking the list from `SOURCES` in the `Makefile`
keeps it correct without maintaining a second copy.

A bounds violation aborts with a TCC diagnostic rather than failing an
assertion, so treat any abort as a defect in the code under test. Verified
against v0.1.113: 68 parser and validation cases pass instrumented.

Useful focused checks while developing are:

```sh
./cbs check recipe.cbs
./cbs explain recipe.cbs --json
./cbs doctor recipe.cbs --arch x86_64 --staged /tmp/cbs-doctor
./tests/cli-contract-test.sh ./cbs
./tests/tool-policy-test.sh ./cbs
```

## Check documentation and release state

Before tagging, confirm that `VERSION`, the current-release statements, and
the intended tag agree:

```sh
sed -n '1p' VERSION
git status --short --branch
git diff --check
```

A release build invokes `version-check`; a checkout at tag `vX.Y.Z` must have
`X.Y.Z` as the first line of `VERSION`. Keep the executable, static archive,
public header, and pkg-config metadata from the same tag.

## Release checklist

1. Update implementation, tests, guides, and current release references.
2. Run `git diff --check`, then `make -o version-check test`. At a tagged
   checkout `version-check` refuses the bumped `VERSION` until the commit in
   step 4 moves `HEAD` off the previous tag, so the full `make -j1 test` runs
   after that commit and again from the new tag in step 7.
3. Confirm the upstream qualification result is either passed or explicitly
   skipped because `CBS_UPSTREAM_CACHE` is unset.
4. Commit the change with a focused message.
5. Create the matching annotated tag, for example `git tag -a v0.1.113 -m
   "CBS v0.1.113"`.
6. Push the branch and tag together.
7. Re-run `make -j1 test` from the tagged checkout.
8. Update the issue tracker with the commit, tag, test result, and any
   intentional qualification skip.

Do not describe cixd-owned sandboxing, image composition, repository
publication, signing, or transaction behavior as a CBS release feature. Those
boundaries are defined in the integration contract.
