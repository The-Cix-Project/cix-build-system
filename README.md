# Cix Build System

CBS is the command-line package build engine for Cix. It reads a package
definition written in CPDL — one `.cbs` file — and produces a verified CIXPKG
artifact, with no daemon and no shell involved at any point.

A build is a fixed pipeline: validate the definition, fetch and verify every
declared source against its checksum, execute the declared phases through
direct `execve`, generate a deterministic manifest from the staged tree, and
emit an artifact that `cbs verify` can check on its own. Filesystem effects are
confined to CBS-supplied roots. `cixd` is an optional integration point for
stronger sandboxing and image transactions; nothing here requires it.

CPDL is deliberately not a general programming language. It has no shell
escape, no pipelines, no variables, and no runtime control flow, because a
recipe that can reach the host cannot be reasoned about or reproduced.

Current release: **CBS v0.1.113** — the first line of `VERSION`, with the
matching `v<version>` tag. `make` refuses a mismatch when building from a tag.

## Where to go

| If you want to | Read |
| --- | --- |
| Write your first recipe | [user manual](docs/user-manual.md) |
| Operate or script CBS | [standalone runbook](docs/standalone-runbook.md) |
| Know exactly what CPDL accepts | [CPDL 1.0 specification](docs/spec/cpdl-1.0.md) |
| Embed CBS or drive it from a parent | [cixd embedding guide](docs/guides/cixd-embedding.md) |
| Know what this repository does and does not own | [repository status](docs/repository-status.md) |
| Cut a release | [CI and release guide](docs/guides/ci-and-release.md) |

The complete documentation index is [docs/README.md](docs/README.md).

## Build

TCC is the only supported compiler. The executable links against libarchive,
zstd, and the platform dynamic-loader library. Cache misses in standalone
source fetching additionally require a compatible runtime libcurl:

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

The suite runs entirely offline. It covers accepted and rejected definitions,
diagnostic shape and location, execution, archive safety, CIXPKG round trips,
byte-identical rebuilds, and the CLI contracts; `docs/cpdl-test-coverage.md`
maps each surface to the test that asserts it. The one step that does not run
by default is the real-package qualification, which needs a digest-keyed source
cache:

```text
make upstream-test CBS_UPSTREAM_CACHE=/path/to/source-cache
```

Maintainer-level checks, including TCC bounds instrumentation, are in
[the CI and release guide](docs/guides/ci-and-release.md).

## Command-line use

`./cbs --help` is the authoritative command list. In outline:

| Command | Does |
| --- | --- |
| `check` / `validate` | validate a recipe without executing anything; the two spellings are equivalent and both accept `--json` |
| `explain` | print the validated execution plan; `--json` for machines |
| `doctor` | preflight a recipe and an image without executing or mutating |
| `fingerprint` | compute the deterministic build key before building |
| `build` | execute the recipe and emit a CIXPKG |
| `verify` | check an artifact on its own, without its recipe |
| `list` / `diff` | read and compare verified manifests |
| `extract` | unpack a verified artifact |
| `revise` | change a recipe's version or source coordinates byte-preservingly |
| `package` | package a tree a caller assembled itself |
| `inspect` | report recipe, source, and artifact digests |

Every command that can fail accepts `--diagnostics=jsonl`.

A first build looks like this; the
[user manual](docs/user-manual.md) explains each step and the
[standalone runbook](docs/standalone-runbook.md) is the operator's version:

```text
mkdir -p /tmp/cbs-workspace
./cbs check path/to/package.cbs
./cbs build path/to/package.cbs \
    --arch x86_64 \
    --staged /tmp/cbs-workspace \
    --output package.cixpkg \
    --cache /var/cache/cbs/sources
./cbs verify package.cixpkg
```

`--staged` names a workspace root that must already exist; CBS creates `src`,
`build`, `dest`, `cache`, and `tmp` inside it. `--cache` is optional: CBS
verifies every cache entry against the recipe's SHA-256 before use, and on a
miss the standalone CLI fetches HTTP/HTTPS sources with libcurl and verifies
them before caching.

Exit statuses are `0` success, `2` CLI usage, `3` recipe, source or build
failure, and `4` artifact verification or extraction failure — fixed by
ADR-0008 and specified in
[the CPDL specification](docs/spec/cpdl-1.0.md) §9.

## Install

```text
make install PREFIX=/usr/local
```

This installs `cbs`, `libcbs.a`, `cbs/cbs.h`, and a pkg-config file. The
library boundary is a static archive, not a shared object or plugin loader; see
[the library guide](docs/library.md) and
[the integration contract](docs/integration-contract.md).

## Status and scope

What this repository owns, and what belongs to `cixd`, is stated in
[repository status](docs/repository-status.md). Remaining integration inputs are
in [the blocker register](docs/integration-blockers.md), and the workstream view
is in [the roadmap](docs/roadmap.md). Open work is tracked in the issue tracker,
not in these documents.
