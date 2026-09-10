# CBS user manual

This manual takes you from a fresh CBS checkout to a verified CIXPKG package.
CBS consumes `.cbs` files written in the Cix Package Definition Language (CPDL)
and produces packages containing a deterministic manifest and compressed staged
file payload.

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

`explain` performs the same validation and then prints the ordered phases and
their operation counts. It is non-executing and useful for reviewing a recipe
before allowing a build.

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

## 11. Troubleshooting

`recipe must use the .cbs extension` means the input filename is not accepted.

`source ... is not cached` or a libcurl transport message means the source could
not be downloaded. Check the URL, network/TLS environment, cache permissions,
and the declared SHA-256. A wrong digest is always rejected.

`build failed` means a phase, workspace, manifest, or package write failed.
Run `check` first, confirm the workspace root exists, and inspect the located
runtime diagnostic emitted before the final summary.

`artifact verification failed` means the file is truncated, corrupted, has
been modified, or does not conform to CIXPKG v1. Never extract an artifact that
does not verify.

For the complete grammar and diagnostic contract, see the [CPDL specification](spec/cpdl-0.1.md).
