# Cix Build System

Current release: **CBS v0.1.68**. The release version is the first line of
`VERSION`; release tags use the matching `v<version>` spelling. `make` checks
that relationship when building from a release tag.

CBS is the command-line package build engine for Cix. It consumes package
definitions written in CPDL and stored as `.cbs` files.

The standalone CPDL 0.1 pipeline is implemented. The executable can lex, parse,
validate, execute package phases, generate a deterministic manifest, package the
staged file payload, and emit a verified CIXPKG artifact without requiring cixd.

The production runtime implements direct `execve` execution for validated `run`
AST nodes, the CPDL 0.1 filesystem vocabulary, and atomic source edits and
assertions in C. A single block executor preserves primary failures while
running subordinate `on_fail` diagnostics. Filesystem access is confined to
CBS-supplied roots and does not invoke host utilities. cixd remains an optional
integration point for stronger sandboxing and image transactions.

Package identity has one canonical representation built from recipe name,
upstream version, Cix release, and the CBS-supplied target architecture. Runtime
values, artifact filenames, and digest metadata derive from that representation.

Named sources pair one checksum with one or more ordered mirror URLs. CBS uses
its internal SHA-256 implementation and exposes source paths to CPDL only after
every declared source has verified successfully.

## Build

TCC is the only supported compiler. The runtime also requires libcurl (for
standalone HTTP/HTTPS source fetching), libarchive, and zstd:

```text
make
```

The build treats every compiler warning as an error and produces `./cbs`.
Python is not required to build or test CBS; the local HTTP regression server
is compiled from C as part of `make test`.

## Test

```text
make test
```

The regression suite validates accepted and rejected definitions, diagnostic
shape and locations, UTF-8 handling, CRLF normalization, the `.cbs` extension,
and the guarantee that validation does not execute phases.

For TCC bounds instrumentation:

```text
tcc -b -Isrc -std=c11 -Wall -Wextra -Werror -pedantic \
    src/ast.c src/diag.c src/exec.c src/fs.c src/lexer.c src/main.c \
    src/parser.c src/validate.c \
    -o /tmp/cbs-bounds
./tests/parser-validation.sh /tmp/cbs-bounds
```

## Command-line use

Run `./cbs --help` for the complete command list. `check` is the intuitive
alias for `validate`; `build` executes a recipe and emits a CIXPKG; `inspect`
reports recipe/source/artifact digests; and `verify` validates a CIXPKG without
requiring its recipe.

Use `explain` to print the validated execution plan without executing it:

```text
./cbs explain path/to/package.cbs
# Add --json for scriptable plan inspection.
./cbs explain path/to/package.cbs --json
```

The complete first-time-user workflow is documented in the
[CBS user manual](docs/user-manual.md).

To install the executable, static library, and public embedding header after a
successful build:

```text
make install PREFIX=/usr/local
```

This installs `cbs`, `libcbs.a`, and `cbs/cbs.h`. The library boundary is a
static-library API; the cixd first-slice integration remains the documented
child-process contract in [the integration contract](docs/integration-contract.md).

The prioritized implementation plan is tracked in the
[CBS delivery roadmap](docs/roadmap.md).

## Validate a package definition

```text
./cbs validate path/to/package.cbs
```

Successful validation prints one confirmation line and exits with status 0.
Recipe I/O, lexical, parse, and validation failures use status 3 and emit the
located diagnostic contract defined in
[`docs/spec/cpdl-0.1.md`](docs/spec/cpdl-0.1.md).

Validation is non-executing: it does not fetch sources, inspect the host
filesystem, resolve dependencies, or spawn phase commands.

## Build a package

The `--staged` argument names a CBS workspace root. It must already exist;
CBS creates its `src`, `build`, `dest`, `cache`, and `tmp` subdirectories.

```text
mkdir -p /tmp/cbs-workspace
./cbs build path/to/package.cbs \
    --arch x86_64 \
    --staged /tmp/cbs-workspace \
    --output package.cixpkg
./cbs verify package.cixpkg
./cbs extract package.cixpkg --into /tmp/cbs-extracted
```

To use a shared, pre-populated source cache, add `--cache CACHE_DIR`:

```text
./cbs build path/to/package.cbs \
    --arch x86_64 \
    --staged /tmp/cbs-workspace \
    --output package.cixpkg \
    --cache /var/cache/cbs/sources
```

CBS verifies every cache entry against the recipe’s SHA-256 before use. On a
cache miss, the standalone CLI fetches HTTP/HTTPS sources with libcurl, then
verifies them before caching. cixd can still provide a centralized fetch
service instead.
