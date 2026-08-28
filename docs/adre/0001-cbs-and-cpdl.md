# ADRE-0001: Establish CBS and CPDL

- Status: Proposed
- Date: 2026-08-28
- Owners: Cix project
- Scope: CBS, CPDL, and their package artifact boundary

## Decision

Create two deliberately separate components:

- **CBS (Cix Build System)** is the engine and command-line interface that
  fetches and verifies sources, evaluates package definitions, controls builds,
  creates packages, verifies packages, and installs packages.
- **CPDL (Cix Package Definition Language)** is the small, purpose-built
  language consumed by CBS. A `.cpdl` file defines one exact package build.

CPDL defines how verified inputs become a staged filesystem tree. CBS owns the
policy and machinery around that definition, including source acquisition,
sandboxing, resource limits, hashing, manifests, normalization, packaging,
artifact retrieval, verification, installation, and file ownership.

CBS will be implemented in C, must compile with TCC, and will have no external
userspace runtime or tool dependencies for its own core operations. Package
builds may invoke tools declared by their CPDL definitions; those are package
dependencies, not CBS dependencies.

CBS will turn the staged tree into one deterministic, self-describing,
compressed `.cixpkg` file suitable for distribution. CPDL recipes do not script
the package container or compression process.

## Context

Cix package recipes are currently shell programs. A demanding recipe such as
GCC combines package metadata, multiple checksummed sources, bootstrap
requirements, build and runtime dependencies, source changes, subprocess
execution, filesystem manipulation, assertions, failure diagnostics, staging,
and packaging policy in one procedural script.

Shell makes the recipe responsible for mechanics that should be common and
consistent across every package: quoting, word splitting, globbing, archive
handling, hashing, failure propagation, package creation, and policy. It also
makes recipes difficult to inspect without executing arbitrary code.

Cix needs a representation that records package intent while retaining enough
controlled imperative behavior to build difficult real-world software.

## Design principles

### A package definition, not a general programming language

CPDL is a structured package definition format with execution semantics. It is
not a replacement for Bash, Python, Make, or Lua. Features are added only when
real package definitions demonstrate that the concept belongs in the language.

The initial language will not include user-defined functions, classes, modules,
general arithmetic, command substitution, pipelines, arbitrary control flow,
or an implicit shell escape.

### Intent is structural

Relationships that CBS understands must be expressed structurally. In
particular:

- each source is directly paired with its checksum;
- upstream `version` is separate from Cix `release`;
- dependency roles are explicit;
- build phases have defined meanings; and
- assertions and expected outcomes are first-class operations.

One recipe revision defines one exact package. Configurable feature matrices and
general version constraints are outside CPDL v0.1.

### No implicit shell

`run` represents an executable and an argument vector. It never performs shell
parsing, word splitting, variable expansion, command substitution, pipelines,
or implicit glob expansion.

For example:

```cpdl
run "make" {
    "bootstrap"
    "LIMITS_H_TEST=true"
    jobs $jobs
}
```

CBS supplies that argument vector directly to the process execution interface.
Filesystem globs are evaluated by CBS only where CPDL explicitly permits a
`glob` value.

### A strict bootstrap boundary

CBS may rely only on the Cix/Linux kernel ABI and the C runtime included in the
Cix base. Core CBS behavior must not require external commands or libraries such
as a shell, `curl`, `tar`, compression tools, checksum tools, or filesystem
utilities.

CBS therefore owns, in C, the functionality needed for its job, including:

- CPDL lexing, parsing, validation, and execution;
- process execution and diagnostics;
- filesystem operations, walking, and glob matching;
- source and artifact hashing;
- supported source archive extraction;
- dependency resolution;
- manifest generation and verification; and
- `.cixpkg` creation, compression, inspection, verification, and installation.

An upstream build system may require `make`, `m4`, a compiler, or other declared
inputs. These belong to the package's build environment and do not weaken the
CBS bootstrap boundary.

### Determinism and trust

CBS will distinguish at least three kinds of digest:

1. source digests identify the exact upstream inputs;
2. a recipe digest identifies the exact CPDL definition; and
3. an artifact digest identifies the exact distributable package.

The artifact server supplies bytes and is not a trust boundary. The expected
artifact digest belongs in trusted package/repository metadata.

Given the same sources, CPDL definition, architecture, and CBS policy/version,
CBS should produce a byte-identical package. CBS therefore owns deterministic
entry ordering, manifest ordering, identity normalization, timestamp policy,
permission preservation, and fixed compression parameters.

## Responsibility boundary

The build lifecycle is divided as follows:

| Stage | Owner | Recipe-programmable |
| --- | --- | --- |
| Fetch sources | CBS | No |
| Verify sources | CBS | No |
| Extract the main source | CBS | No |
| Prepare source | CPDL | Yes |
| Configure | CPDL | Yes |
| Build | CPDL | Yes |
| Check | CPDL | Yes |
| Install into `$dest` | CPDL | Yes |
| Validate staged tree | CBS | Policy-driven |
| Normalize and manifest | CBS | No |
| Create and compress `.cixpkg` | CBS | No |
| Verify/install `.cixpkg` | CBS | No |

The normal data flow is:

```text
CPDL + verified sources
          |
          v
 prepare -> configure -> build -> check -> install
                                           |
                                           v
                                      staged tree
                                           |
                                           v
                           manifest -> normalize -> package
                                           |
                                           v
                          name-version-release-arch.cixpkg
```

## CPDL v0.1 model

### Package identity

A definition starts with a name, upstream version, and Cix release:

```cpdl
package "gcc" {
    version "16.2.0"
    release 11
}
```

Architecture is part of artifact identity and is normally supplied by CBS. A
future `architecture any` declaration is reserved for architecture-independent
packages.

The resulting identity is conceptually:

```text
gcc-16.2.0-11-x86_64
```

### Sources

Sources are named so URLs and hashes cannot be matched positionally by mistake:

```cpdl
sources {
    main "gcc" {
        url "https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz"
        sha256 "e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e"
    }

    extra "gmp" {
        url "https://gcc.gnu.org/pub/gcc/infrastructure/gmp-6.3.0.tar.bz2"
        sha256 "ac28211a7cfb609bae2e2c8d6058d66c8fe96434f740cf6fe2e47b000d1c20cb"
    }
}
```

The main source is extracted by CBS into `$src`. Extra sources are fetched and
verified, then made available by name through `$source.NAME` for explicit use
during `prepare`.

### Dependencies

Dependency roles describe when an input must exist:

| Role | Meaning |
| --- | --- |
| `build` | Required while constructing the package |
| `runtime` | Required for the installed package to perform its intended function |
| `test` | Required only by the `check` phase |
| `bootstrap` | Exceptional seed input used to reach a self-hosting result |

Dependency kinds describe why an input exists, initially including `tool`,
`library`, `headers`, `compiler`, and `package`.

```cpdl
requires {
    build {
        tool "binutils"
        tool "m4"
        library "zlib"
        headers "libc-dev"
    }

    runtime {
        tool "binutils"
        library "zlib"
        headers "libc-dev"
    }

    test {
        tool "dejagnu"
    }

    bootstrap {
        compiler "gcc"
    }
}
```

`runtime` is functional rather than limited to ELF `DT_NEEDED` entries. For
example, development headers may be a legitimate runtime requirement of an
installed compiler. CBS may compare declarations with observed ELF, interpreter,
tool, symlink, and ownership relationships.

CPDL v0.1 names exact repository packages without version expressions. The Cix
repository selects its pinned package graph; v0.1 does not require a general
dependency solver.

### Phases and execution

The recipe-programmable phases are ordered:

```text
prepare -> configure -> build -> check -> install
```

CBS supplies a deliberately small set of immutable values:

```text
$name      $version   $release   $arch
$src       $build     $dest      $jobs
$source.NAME
```

`$jobs` is policy chosen by CBS from CPU, memory, sandbox, and administrator
limits. Recipes do not calculate host parallelism.

Environment bindings use lexical block scope. Command-local bindings affect
only that `run` operation.

The initial filesystem vocabulary is expected to include `mkdir`, `copy`,
`move`, `remove`, `symlink`, `write`, `chmod`, and `extract`. Source edits and
validation use operations such as `replace`, `insert`, `require`, and explicit
`glob` matching. Replacement counts and glob cardinality can be asserted.

`on_fail` runs diagnostics without consuming or replacing the original failure.
`allow_failure` must be explicit for a diagnostic command whose failure is
acceptable.

Package-specific native helpers may temporarily cover genuinely exceptional
operations. Helpers must be named, audited CBS capabilities—not arbitrary shell
programs disguised as language features. A repeated need across unrelated
packages is evidence for a future general CPDL operation.

## `.cixpkg` requirements

A `.cixpkg` is one physical, compressed file with a format implemented by CBS.
It must be:

- deterministic;
- self-describing without executing CPDL;
- inspectable before payload extraction;
- streamable or bounded-memory where practical;
- independently verifiable;
- able to carry package identity and runtime dependency metadata;
- able to carry a per-file manifest including path, type, mode, size, and
  digest where applicable; and
- safe against path traversal and unsafe filesystem entries during extraction.

The logical layout is:

```text
CIXPKG
|- header and format version
|- package and dependency metadata
|- file manifest and payload index
|- compressed payload
`- integrity data
```

The container is not specified as `tar` plus an external compressor. CBS writes,
reads, verifies, and installs the format itself.

## Initial CBS command surface

The intended command model includes:

```text
cbs build <package>       # build through the staged tree
cbs check <package>       # run the package checks
cbs package <package>     # produce a deterministic .cixpkg
cbs install <package>     # prefer a verified artifact, or build by policy
cbs inspect <artifact>    # inspect metadata and manifest without executing CPDL
cbs verify <artifact>     # independently verify the artifact
```

Exact command behavior and repository operations will be specified separately.

## Consequences

### Benefits

- Recipes become inspectable data with constrained execution semantics.
- Common security, reproducibility, and packaging policy has one implementation.
- Dependency roles and source/checksum relationships become machine-readable.
- Simple packages remain small while difficult packages retain controlled escape
  hatches.
- CBS forms a clear and auditable bootstrap boundary for Cix.
- Installation does not require evaluating the package's CPDL definition.

### Costs and risks

- CBS must own non-trivial archive, compression, networking, and filesystem code.
- Supporting common upstream compression formats without external tools is
  substantial work, especially XZ/LZMA.
- A custom package container requires careful versioning, corruption handling,
  fuzzing, and security review.
- The language can become a general scripting language unless additions remain
  evidence-driven.
- Package-specific helpers can become an unstructured second language unless
  tightly governed.

## Rejected alternatives

### Continue using shell recipes

Rejected because shell syntax entangles package intent with ambient tools,
quoting, error propagation, and repeated policy implementations.

### Embed an existing general-purpose language

Rejected because it expands the bootstrap and audit surface and provides far
more capability than package definitions require.

### Make packaging a CPDL phase

Rejected because package layout, normalization, compression, integrity, and
repository trust are CBS policy. A recipe's output boundary is the staged tree.

### Use `tar` plus an external compression program for `.cixpkg`

Rejected because CBS core behavior must not depend on external userspace tools.

### Treat all dependencies as one list

Rejected because build, runtime, test, and bootstrap requirements have different
lifecycle and trust meanings.

## Open decisions

The following are intentionally not fixed by this ADRE:

1. Does “zero external dependencies” prohibit only runtime/link dependencies,
   or also all incorporated third-party source? This materially affects archive,
   decompression, and networking implementations.
2. Which compression algorithm and exact binary layout will CIXPKG v1 use?
3. Which upstream source formats must CBS v1 extract (`tar`, gzip, bzip2, XZ,
   and others), and will support be staged?
4. Does CBS perform networking itself, or consume bytes from a lower Cix service
   whose interface is part of the base boundary?
5. What sandboxing and dependency-observation guarantees are mandatory in v1?
6. What is the exact CPDL v0.1 grammar and diagnostic contract?
7. How are repository metadata, artifact signatures, upgrades, conflicts, and
   transactional installation represented?

Each material answer should be captured by a focused follow-up ADRE or formal
specification.

## Acceptance criteria

This decision is ready to move from Proposed to Accepted when the project agrees
that:

- CBS and CPDL are separate components with the responsibility boundary above;
- CBS core operations have zero external userspace dependencies;
- CPDL has no implicit shell and is not a general-purpose language;
- dependencies have explicit lifecycle roles;
- `$dest` is the CPDL/CBS packaging boundary; and
- CBS owns a deterministic, single-file `.cixpkg` artifact.

