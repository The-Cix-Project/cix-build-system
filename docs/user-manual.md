# CBS user manual

This manual takes you from a fresh CBS checkout to a verified CIXPKG package.
CBS consumes `.cbs` files written in the Cix Package Definition Language (CPDL)
and produces packages containing a deterministic manifest and compressed staged
file payload.

This is the practical, definitive guide to writing CPDL 0.1 recipes and using
the standalone `cbs` command. The [CPDL specification](spec/cpdl-0.1.md) is
the normative grammar and diagnostic reference; this manual explains how to
apply it.

### At a glance

- [Install and first recipe](#1-prerequisites)
- [Validate and inspect](#3-validate-before-building)
- [Build and package](#4-build-a-package)
- [Recipe reference](#11-recipe-reference)
- [Complete authoring workflow](#12-the-complete-authoring-workflow)
- [Security and language limits](#16-security-and-deliberate-language-limits)
- [Troubleshooting](#17-troubleshooting)

## 1. Prerequisites

CBS is currently built with TCC and links against libarchive, zstd, libcurl,
and the platform dynamic-loader library. On a Debian-like system, install the
compiler and development packages before building:

```text
sudo apt install tcc libarchive-dev libzstd-dev libcurl4
```

The current checkout can also build in the project environment when the libcurl
runtime is present. Build CBS from the repository root:

```text
make
./cbs --version
```

Run the regression suite before relying on a build:

```text
make test
```

## 2. The first recipe

CPDL is declarative. A recipe declares package identity and a fixed sequence of
optional phases: `prepare`, `configure`, `build`, `check`, and `install`.
Filesystem effects must use CBS-provided roots such as `$src`, `$build`, and
`$dest`.

Create `hello.cbs`:

```cbs
package "hello" {
    version "1"
    release 1

    build {
        mkdir "${dest}/usr/bin"
        write "${dest}/usr/bin/hello" "hello from CBS\n" chmod 0755
        require file "${dest}/usr/bin/hello" {
            exists
            contains "hello from CBS"
        }
    }
}
```

Important rules:

- Recipe files must end in `.cbs`.
- Package names use lowercase letters, digits, `+`, `.`, and `-`.
- `version` is a non-empty string and `release` is greater than zero.
- At least one build phase is needed to produce a package.
- Commands are argument vectors, not shell command lines.
- Shell expansion, pipelines, command substitution, and ambient environment
  access are not provided.

## 3. Validate before building

Use either `check` or `validate`:

```text
./cbs check hello.cbs
./cbs validate hello.cbs
./cbs explain hello.cbs
```

Validation is non-executing. It does not run commands, fetch sources, inspect
the host filesystem, or resolve dependencies. Errors include the recipe path,
line, column, diagnostic code, and category.

Add `--json` to `check` or `validate` to emit one JSON diagnostic object per
error on standard error, suitable for editor and CI integration:

```sh
./cbs check hello.cbs --json
```

`explain` performs the same validation and then prints the ordered phases and
their operation counts. It is non-executing and useful for reviewing a recipe
before allowing a build. Add `--json` for a machine-readable object containing
the package identity fields, `architecture: null`, all source URLs and SHA-256
digests, dependency groups by kind, and the ordered phase names and operation
counts. Architecture is selected by `build --arch`, not declared in CPDL.

## 4. Build a package

The `--staged` value is a workspace root. Create it first; CBS creates the
`src`, `build`, `dest`, `cache`, and `tmp` directories inside it.

```text
mkdir -p /tmp/cbs-hello
./cbs build hello.cbs \
    --arch x86_64 \
    --staged /tmp/cbs-hello \
    --output hello-1-1-x86_64.cixpkg
```

The build performs these operations in order:

1. Parse and validate the recipe.
2. Prepare the CBS workspace.
3. Fetch, verify, and extract declared sources.
4. Execute declared phases in their fixed order.
5. Generate a sorted manifest from `${dest}`.
6. Compress the manifest and regular-file payload with zstd.
7. Write the CIXPKG artifact.

The target architecture is supplied by `--arch` and becomes part of the
canonical package identity and artifact name.

## 5. Sources and the cache

Sources are declared with one or more URLs and a SHA-256 digest:

```cbs
package "example" {
    version "1.2.3"
    release 1

    sources {
        main "example" {
            url "https://example.org/example-1.2.3.tar.xz"
            url "https://mirror.example.org/example-1.2.3.tar.xz"
            sha256 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        }
    }

    prepare {
        require directory $src {
            exists
        }
    }
}
```

The first source must be named `main`. CBS tries URLs in declaration order.
Standalone CBS fetches `http://` and `https://` URLs with libcurl. Each download
is written to a temporary file and is accepted only after its SHA-256 matches
the recipe. Verified bytes are atomically stored under a filename equal to the
digest.

Use a shared cache with `--cache`:

```text
mkdir -p /var/cache/cbs/sources /tmp/cbs-example
./cbs build example.cbs \
    --arch x86_64 \
    --staged /tmp/cbs-example \
    --output example-1-1-x86_64.cixpkg \
    --cache /var/cache/cbs/sources
```

For an internal HTTPS endpoint with a private CA, add that CA explicitly:

```text
./cbs build example.cbs --arch x86_64 --staged /tmp/cbs-example \
    --output example-1-1-x86_64.cixpkg --ca-file /etc/cix/ca.pem
```

This preserves peer and hostname verification and does not replace CBS's
source SHA-256 verification.

Cache hits do not require network access. Source archives are extracted by
CBS's libarchive boundary; archive paths and entry types are checked before
files are exposed to the recipe.

## 6. Writing build phases

Use `run` for direct process execution:

```cbs
build {
    cd $build {
        run "make" {
            "-j${jobs}"
            jobs $jobs
            timeout 10m
            expect exit 0
        }
    }
}
```

Use the built-in filesystem vocabulary for package-tree operations:

```cbs
install {
    mkdir "${dest}/usr/bin"
    copy "${build}/hello" to "${dest}/usr/bin/hello"
    chmod 0755 "${dest}/usr/bin/hello"
    require file "${dest}/usr/bin/hello" {
        exists
    }
}
```

`replace`, `insert`, `write`, `copy`, `move`, `remove`, `symlink`, `extract`,
`materialize`,
`require`, globbing, environment bindings, timeouts, expected exit status, and
`on_fail` diagnostics are specified in the [CPDL specification](spec/cpdl-0.1.md).

Build phases cannot access the network. Source acquisition happens before phase
execution, and undeclared host paths are rejected.

## 7. Inspect, verify, and extract

Inspect recipe and optional artifact digests:

```text
./cbs inspect hello.cbs
./cbs inspect hello.cbs hello-1-1-x86_64.cixpkg
```

Verify an artifact without the recipe:

```text
./cbs verify hello-1-1-x86_64.cixpkg
```

Extract a verified artifact into a destination that does not already exist:

```text
./cbs extract hello-1-1-x86_64.cixpkg --into /tmp/cbs-hello-extracted
```

Extraction verifies the manifest, payload, and every regular-file digest before
publishing the destination. Unsafe paths and partial destinations are rejected.

The CIXPKG format is documented in the [CIXPKG specification](spec/cixpkg-1.0.md).

## 8. Exit statuses

```text
0  operation completed successfully
2  invalid command-line usage
3  recipe, source, build, or runtime failure
4  artifact verification or extraction failure
```

The most useful first diagnostic is the located `CPDL-E####` or
`CIXPKG-E####` message. For a runtime failure, inspect the reported phase and
the command's expected versus actual exit status.

## 9. Reproducibility and security

Keep the recipe, source digests, CBS version, architecture, cache provenance,
and resulting artifact together. CBS uses canonical package identity, sorted
manifest paths, fixed zstd parameters, explicit modes, and independent digest
checks to make equivalent builds comparable.

CBS never passes recipe text through a shell. `run` executes a declared program
directly. Source downloads use libcurl in-process, are restricted to HTTP/HTTPS,
and are independently verified by CBS. Build-phase network access and writes
outside CBS-owned roots are forbidden.

## 10. Standalone CBS and cixd

Standalone CBS can fetch sources, build packages, verify artifacts, and extract
them without a daemon. cixd remains useful when you need centralized transport
policy and audit logging, stronger OS sandboxing, dependency observation, or
immutable image transactions. It can supply the existing fetch-service callback
and other adapter hooks; it does not change CPDL recipe semantics.

## 11. Recipe reference

This section is the practical reference for writing a package. A complete
package has this shape:

```cbs
package "name" {
    version "upstream-version"
    release 1
    upstream "kernel.org"                  # optional registered provider

    sources { ... }                         # optional, one main source
    requires { ... }                        # optional dependency declarations
    build_image "image-name"                # optional executor metadata
    capability "CAP_EXAMPLE"                # repeatable executor metadata
    toolchain "gcc" {                       # explicit compiler exception
        reason "why the exception is required"
    }

    prepare { ... }
    configure { ... }
    build { ... }
    check { ... }
    install { ... }
}
```

Package items must appear in this order: identity, upstream, sources,
requirements, execution metadata, then phases. Each phase is optional and may
appear once. Empty phases are allowed syntactically but are rarely useful.
Phase order is always `prepare`, `configure`, `build`, `check`, `install`.

`upstream` names a release-discovery provider. It does not replace the pinned
source URL or digest in CPDL 0.1. `kernel.org` is currently the registered
provider. `build_image` and `capability` describe what an orchestrator must
provide; standalone CBS validates and reports them but cannot create an image
or grant a Linux capability. A compiler other than TCC is rejected unless the
recipe declares the matching `toolchain` exception with a non-empty reason.

### 11.1 Values and interpolation

Use quoted strings for paths, arguments, and generated text. Use block strings
for multiline files or source edits:

```cbs
write "${build}/config.h" """
#define FEATURE 1
#define NAME "example"
"""
```

Block strings have no escapes or interpolation. Quoted strings support these
values:

```text
$name       $version       $release       $arch
$triplet    $src           $build         $dest        $jobs
$source.NAME
```

The `${...}` form can be embedded in a quoted string. Interpolation produces
one value; it never performs shell splitting, glob expansion, or command
substitution. Thus `"--prefix=${dest}/usr tree"` is one argument. `$source.NAME`
is available only for a declared source and only after CBS verifies all
declared sources.

The literal characters `$`, `*`, `;`, quotes, and spaces are ordinary bytes
unless they occur in the exact interpolation form. `$(...)`, `$NAME`, and
backslash-dollar are not CBS features.

### 11.2 Sources

Declare exactly one `main` source and any number of `extra` sources. Each source
has one or more ordered mirror URLs and one SHA-256 digest:

```cbs
sources {
    main "project" {
        url "https://example.org/project-1.0.tar.xz"
        url "https://mirror.example.org/project-1.0.tar.xz"
        sha256 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
    extra "config" {
        url "https://example.org/project.config"
        sha256 "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
    }
}
```

CBS tries mirrors in order, verifies every downloaded or cached file before
exposing it, and caches verified bytes by digest. The main source is extracted
under `$src/<source-name>`. An extra source remains a verified file and must be
used explicitly:

```cbs
prepare {
    extract $source.config into $build as "config-input"
    materialize $source.config to "${build}/config.raw"
}
```

`extract` is for supported archives; `materialize` is for a regular file that
must remain byte-for-byte unchanged. Recipes cannot read arbitrary cache paths.
Use `--ca-file FILE` only when a private CA is required. TLS peer and hostname
verification remain enabled, and the recipe digest is still authoritative.

### 11.3 Dependencies

Dependencies are grouped by role and kind:

```cbs
requires {
    build {
        compiler "tcc"
        tool "make"
        headers "libfoo-dev"
        library "libfoo"
    }
    runtime {
        library "libfoo"
    }
    test {
        tool "tester"
    }
    bootstrap {
        compiler "tcc"
    }
}
```

Roles are ordered `build`, `runtime`, `test`, `bootstrap`. Dependency names
must be lowercase package-style names. Dependencies are declarations, not a
solver or version constraint language. Standalone CBS validates them and
passes the recipe to its configured build environment; it does not resolve an
image dependency graph. `compiler "tcc"` is the normal policy. For the one
currently supported exception, use:

```cbs
requires { build { compiler "gcc" } }
toolchain "gcc" { reason "Linux kbuild requires GCC extensions" }
```

For post-build inspection, `cbs_observe_dependencies` reads a little-endian
ELF64 file directly and calls its observer once for each `DT_NEEDED` library
name. It rejects non-ELF, malformed, truncated, or unsupported files and
propagates an observer failure. This is observation, not dependency solving:
an embedder can compare the observed names with the recipe’s declared runtime
libraries and report undeclared or missing requirements. Standalone CBS does
not silently resolve or install those libraries.

### 11.4 Processes and environments

`run` always names one executable followed by one argument per string:

```cbs
configure {
    cd $build {
        env "CC" = "tcc"
        run "${src}/project-1.0/configure" {
            "--prefix=/usr"
            "--disable-nls"
            timeout 10m
            expect exit 0
        }
    }
}
```

Use `env "NAME" = VALUE` at block scope for later operations, or inside a
`run` block for one command only. Environment names match
`[A-Z_][A-Z0-9_]*`. `jobs $jobs` declares resource usage; it does not invent a
`-j` argument, so pass a tool-specific option explicitly when needed.

CBS calls the executable directly. It does not invoke `sh`, `bash`, `env`,
`system`, `popen`, `execvp`, pipelines, redirections, command substitution, or
implicit `PATH` expansion. A verified upstream script such as `configure` may
be run directly because its own shebang is interpreted by the kernel. Recipe
logic must be expressed with CPDL operations, not a shell wrapper.

Each command may have one `jobs`, `timeout`, and `expect exit N` option. The
default expected status is zero. `allow_failure` is permitted only inside an
`on_fail` block and does not turn the original operation into a success.

### 11.5 Filesystem operations

Use `$src`, `$build`, and `$dest` rather than host paths. Common operations are:

```cbs
prepare {
    mkdir "${build}/generated" chmod 0755
    write "${build}/generated/version.h" "#define VERSION 1\n"
    copy "${src}/project-1.0/LICENSE" to "${build}/LICENSE"
    move "${build}/old" to "${build}/new"
    symlink "/usr/bin/project" to "${dest}/usr/bin/project-current"
    chmod 0644 "${build}/LICENSE"
    remove tree "${build}/temporary"
}
```

`copy` is not recursive. `move` stays within the CBS filesystem and does not
fall back to copy-and-delete across filesystems. `remove tree` is required for
non-empty directories. Symlink targets are stored exactly as written, while
the link destination is confined. A glob selector is explicit:

```cbs
chmod 0755 glob "${dest}/usr/bin/*"
remove glob "${build}/**/*.tmp"
require glob "${build}/**/*.o" { count 3 }
```

Globs are bytewise, case-sensitive, and never traverse symlinked directories.
`*` matches within one path component; `**` matches path components. A literal
path containing `*` is not a glob unless `glob` is written.

### 11.6 Edits and assertions

Edits are exact byte operations and require an expected match count:

```cbs
prepare {
    replace "${src}/project-1.0/Makefile" {
        from "PREFIX = /usr/local"
        to "PREFIX = /usr"
        exactly 1
    }
    insert "${src}/project-1.0/config.h" {
        after "#define HEADER_H"
        write "\n#define FEATURE 1"
        exactly 1
    }
}
```

An empty match and an unexpected count fail before mutation. Assertions include:

```cbs
check {
    require file "${build}/output" {
        exists
        contains "expected bytes"
        same_as "${build}/reference-output"
    }
    require directory "${dest}/usr/bin" { exists }
    require config "${build}/.config" {
        CONFIG_FEATURE = y
        CONFIG_OPTIONAL = absent
    }
}
```

`contains` searches literal bytes. `same_as` compares two confined regular
files byte-for-byte. Config assertions understand `y`, `m`, `n`, and `absent`,
including Linux’s `# CONFIG_NAME is not set` spelling for `n`.

## 12. The complete authoring workflow

When porting a shell recipe, use this order:

1. Copy package identity, source URLs, and verified digests.
2. Convert every build tool and runtime library into `requires` entries.
3. Extract archives with `extract`; never call `tar`, `gzip`, `xz`, or a shell
   merely to unpack a declared source.
4. Convert `cd`, environment assignments, and command arguments into `cd`,
   `env`, and direct `run` operations.
5. Convert `sed`, `cat`, and generated files into exact `replace`, `insert`, and
   `write` operations.
6. Convert shell tests into `require`, `same_as`, `contains`, config, or glob
   assertions.
7. Put all install output below `$dest`; do not write directly to `/usr`,
   `/etc`, `/lib`, `/run`, or another host path.
8. Add `build_image`, `capability`, `upstream`, or `toolchain` metadata when
   the original recipe declares those policies.
9. Run `check`, then `explain`, then build in a fresh workspace.
10. Verify and extract the resulting artifact independently.

Do not translate shell loops, conditionals, command substitution, or dynamic
path discovery literally. First determine whether the behavior is a general
CBS operation, a fixed set of explicit operations, or an executor/image
responsibility. Package-specific native helpers are exceptional and require
the governance in ADR-0004.

## 13. Build outputs and provenance

The package identity is `(name, version, release, architecture)`. The
architecture comes from `--arch`; it cannot be selected by a recipe. The usual
artifact filename is `name-version-release-architecture.cixpkg`.

The artifact contains a sorted typed manifest and a compressed regular-file
payload. CIXPKG v2 represents regular files, directories (including empty
directories), and symbolic links. Ownership is normalized to root (`uid=0,
gid=0`), permission bits are preserved, and setuid/setgid entries are rejected.
Absolute symlink targets are image-root-relative and may be dangling; relative
targets must remain within the image root. Devices, FIFOs, sockets, and
hardlinks are rejected. Manifest paths, metadata, section digests, and
per-file digests are verified before extraction.

For reproducibility, use pinned source digests, deterministic generated files,
explicit modes, fixed package identity, and gates that check actual output
bytes rather than only exit statuses. Keep the recipe digest, source digests,
architecture, build-environment identity, and artifact digest together in the
build record.

## 14. Standalone versus orchestrated builds

Standalone CBS owns parsing, validation, source transport, workspace
confinement, phase execution, manifest generation, CIXPKG creation, verification,
and extraction. It does not resolve package graphs, create Linux namespaces,
grant capabilities, construct build images, manage signing keys, or publish to
a repository. It also does not implement image transactions: atomic image
replacement and rollback belong to the orchestrator that owns image versions.

The cixd/orchestrated path may provide fetch, sandbox, signature, transaction,
dependency, and image services through their explicit adapter boundaries. Those
services must preserve CPDL semantics: they may strengthen isolation and policy,
but may not turn a recipe into shell input or bypass source and artifact
verification.

An embedder that owns platform-wide staged-tree policy can use
`cbs_build_standalone_with_cache_policy()` with a `CbsFinalizePolicy` callback.
CBS calls it after `install` and before manifest generation; a failure aborts
the build. A successful callback sets the CIXPKG v2 finalized-policy flag, so
the artifact records that the external policy step ran. The policy is an API
input, not CPDL syntax, and recipes cannot disable it.

Embedders can also observe execution through `CbsPhaseEvent` in the execution
context. CBS sends `phase-begin` and `phase-end` events in phase order; an end
status of `0` means success and `1` identifies the failed phase. This callback
is optional, and event delivery failure stops execution.

## 15. Quick diagnosis

`recipe must use the .cbs extension` means the input filename is not accepted.

`source ... is not cached` or a libcurl transport message means the source could
not be downloaded. Check the URL, network/TLS environment, cache permissions,
and the declared SHA-256. A wrong digest is always rejected.

`build failed` means a phase, workspace, manifest, or package write failed.
Run `check` first, confirm the workspace root exists, and inspect the located
runtime diagnostic emitted before the final summary.

`artifact verification failed` means the file is truncated, corrupted, has
been modified, uses the retired v1 format, or does not conform to CIXPKG v2.
Never extract an artifact that does not verify.

For the complete grammar and diagnostic contract, see the [CPDL specification](spec/cpdl-0.1.md).

## 16. Security and deliberate language limits

CBS is designed to make package builds reviewable and reproducible. A recipe
is data parsed into an AST, validated, and then executed through a small set of
typed operations. It is not a shell script with a safer default.

The following are deliberately not CPDL features:

- shell commands, command substitution, pipelines, redirections, or glob
  expansion by a process shell;
- `if`, `for`, `while`, functions, variables, arithmetic, or user-defined
  macros;
- arbitrary host-file reads or writes;
- network access from a build phase;
- implicit compiler or dependency discovery;
- recursive `copy` or unrestricted deletion;
- package dependency version solving;
- package signatures or repository publishing in standalone CBS; and
- native package-specific helpers without an approved ADR-0004 registry entry.

When a shell recipe needs one of these, decide which of three cases applies:

1. express the fixed behavior with existing CPDL operations;
2. add a general, security-reviewed CBS operation if multiple packages need the
   capability; or
3. keep it in the build-image/orchestrator layer when it is environment policy.

Do not work around the language boundary by running `bash -c`, `sh -c`, `env`,
or a generated script. CBS rejects command interpreters as recipe executables.

### 16.1 Current CIXPKG limitations

CIXPKG v2 intentionally has no extended-attribute, device-node, FIFO, socket,
hardlink, embedded signature, or v1-reader compatibility. Its digests protect
integrity; detached signatures, key revocation, and repository trust remain
orchestrator responsibilities. A package needing one
of the rejected filesystem types must use a platform-specific installation
mechanism rather than smuggling it through the artifact.

### 16.2 Standalone policy boundaries

Standalone CBS cannot grant `CAP_SYS_ADMIN`, create a named build image, or
provide firmware and other image contents. Recipes may declare `build_image`
and `capability` so an orchestrator can enforce them; `cbs check` validates and
`cbs explain` reports them. Similarly, `upstream` identifies a registered
discovery provider, but CPDL 0.1 still requires a pinned URL and SHA-256 for
the actual build.

## 17. Troubleshooting

### Recipe validation

`package declaration appears out of order` means package items are not in the
required order: identity, upstream, sources, requirements, metadata, phases.
Move the named item rather than duplicating it.

`compiler requires TCC or an explicit toolchain exception` means a dependency
declares another compiler. Use `compiler "tcc"`, or add a documented supported
`toolchain "gcc" { reason "..." }` declaration where the package genuinely
requires GCC-specific behavior.

`unknown CBS interpolation value` means the `${...}` name is not one of the
fixed values listed in section 11.1. CBS has no user-defined variables.

`source ... is not declared` means `$source.NAME` refers to a source name that
does not occur in the package’s `sources` block.

`exactly N` edit failures mean the source changed shape or the needle was
ambiguous. Inspect the fetched source and update the recipe deliberately; do
not remove the cardinality gate.

### Build and process failures

`executable could not be resolved` means the command is not in CBS’s
deterministic executable search path. Declare the tool in the build
environment, or use the verified source-relative path to an upstream script.

`make exited with status N` is a real child-process failure. Check the phase,
resolved working directory, explicit environment bindings, and arguments. CBS
does not reinterpret a nonzero status as success.

`operation timed out` means CBS terminated and reaped the command’s process
group. Reduce jobs, inspect for a deadlock, or set a justified larger timeout;
never hide it with `allow_failure` in the main phase.

CBS also applies child resource limits before execution. Standalone defaults
cover address space, individual file size, CPU time, open descriptors, and
process count; an embedder can lower them through its execution context. These
limits do not provide an aggregate disk quota, so hosted builds must enforce
workspace storage through their container or cgroup policy. Ctrl-C is forwarded
to the active command group and cleaned up through the same reaping path.

### Sources and workspace

A checksum mismatch means the bytes are not the declared source, even when the
URL is trusted. Check redirects, mirrors, proxy rewriting, and whether the
wrong release was downloaded. Delete only the affected digest cache entry and
retry after correcting the declaration.

The staged root must exist before `cbs build` starts and must be dedicated to
that build. If a previous attempt left files behind, use a new empty workspace.
The output path must be separate from the staged root. CBS creates and owns
`src`, `build`, `dest`, `cache`, and `tmp` below the workspace.

### Artifact failures

`CIXPKG-E4001` means verification failed before extraction. Treat the artifact
as corrupt or incompatible; do not bypass verification or extract it manually.
If an expected installed path is missing, inspect the staged v2 manifest and
check its entry type, mode, and target before changing the recipe.
