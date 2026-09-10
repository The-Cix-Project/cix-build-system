# ADR-0002: Bootstrap CBS and permit linked base libraries

- Status: Proposed
- Date: 2026-08-28
- Owners: Cix project
- Scope: CBS bootstrap, self-hosting, and library dependency boundary
- Supersedes: The prohibition on external userspace libraries in ADR-0001

## Decision

CBS may link against carefully selected libraries supplied by the Cix base.
CBS remains the only package-orchestration entry point: linked libraries provide
bounded mechanisms, while CBS owns policy, sequencing, validation, diagnostics,
and the public command-line interface.

The first CBS binary is built by the Cix base bootstrap using TCC. After that
genesis build, the previously installed trusted CBS release builds and packages
the next CBS release from the canonical `cbs.cbs` package definition.

The compiler lineage remains rooted exclusively in TCC. Permitting libraries
does not permit another seed compiler or an ambient compiler fallback.

## Context

ADR-0001 originally required CBS core operations to have no external userspace
tool or library dependencies. Further design discussion refined the intended
boundary:

- external orchestration commands remain undesirable;
- CBS should not reimplement mature, bounded mechanisms merely to claim that no
  libraries are linked;
- the CBS executable must remain lightweight and must own its entry point; and
- the package engine cannot orchestrate its own first installation.

This creates two questions that require explicit answers:

1. Which components are trusted before CBS exists?
2. How does CBS become responsible for its own later releases without creating
   a circular dependency?

## Bootstrap stages

The bootstrap chain is:

```text
Cix base bootstrap
  |
  +-- kernel ABI
  +-- libc
  +-- TCC
  `-- approved base libraries
         |
         v
    build CBS stage 0 with TCC
         |
         v
    stage 0 builds cbs.cbs -> CBS stage 1 package
         |
         v
    stage 1 builds cbs.cbs -> CBS stage 2 package
         |
         v
    compare stage 1 and stage 2 under CBS reproducibility policy
```

### Stage 0

Stage 0 is the genesis CBS executable. It is compiled directly from the CBS
source tree by the repository-owned bootstrap build entry point using TCC and
only approved base libraries.

Stage 0 is not an alternative package engine. It is a build of the same CBS
implementation with the narrow purpose of crossing the point at which CBS can
manage itself.

### Stage 1

Stage 0 evaluates the canonical `cbs.cbs` definition, builds CBS, and emits the
first CBS-managed `.cixpkg`. This proves that the normal package path can build
and package CBS.

### Stage 2

The installed stage 1 CBS repeats the same build from the same declared inputs.
The result is compared with stage 1 according to the deterministic-build policy.
A mismatch is a bootstrap/reproducibility failure and must be diagnosed; it is
not silently accepted.

### Later releases

Once the bootstrap transition is established, CBS is upgraded like any other
package: the currently trusted installed CBS verifies the new inputs, evaluates
`cbs.cbs`, produces the new artifact, and installs it using the normal package
transaction.

The bootstrap path remains maintained and tested so that Cix can still be built
from its documented seed rather than depending forever on a historical CBS
binary.

## Trusted seed

The stage-zero trusted seed consists only of:

- the Cix/Linux kernel ABI;
- the C runtime selected for the Cix base;
- TCC;
- explicitly approved libraries required to construct and run CBS stage 0; and
- the repository-owned CBS source and bootstrap build definition.

Every seed component must be named and versioned by the Cix base build. Ambient
host discovery is forbidden. The presence of an undeclared host library or tool
must not change the selected implementation or build output.

## Library admission rules

A library may enter the CBS base boundary only when all of the following hold:

1. It provides a cohesive mechanism that would be disproportionately costly or
   risky for CBS to reimplement.
2. CBS retains all orchestration policy and exposes no second user-facing entry
   point for the mechanism.
3. Its exact ABI, version, configuration, and license are documented and pinned
   by the Cix base.
4. It is available before CBS without requiring CBS to install it.
5. It builds within the TCC-rooted Cix bootstrap lineage.
6. Its failure is surfaced through CBS diagnostics without silent fallback to a
   command-line tool or parallel implementation.
7. Its security and maintenance cost is justified by concrete CBS requirements.

Library admission is per library, not a blanket permission to accumulate
dependencies. Each admitted library requires a focused ADR or an explicit
amendment naming the responsibility it owns.

## No dependency cycle

CBS must not require a library whose only available installation path requires
CBS. Any library linked by stage 0 must already be part of the independently
constructible Cix base seed.

After CBS exists, it may rebuild and package those libraries through the normal
CBS path. Those rebuilt artifacts can replace their seed counterparts only via
a documented bootstrap transition that preserves the ability to reconstruct
the system from the original seed.

## One implementation and one entry point

For each operation, CBS selects one implementation. It must not maintain both a
library-backed implementation and a command-backed fallback.

For example, if archive extraction is assigned to an approved library, CBS does
not also invoke `tar` when the library rejects an input. Unsupported input fails
with a precise CBS diagnostic. Likewise, networking through an approved library
does not fall back to `curl` or `wget`.

The public workflow remains:

```text
user or Cix tooling -> cbs -> approved library mechanism
```

Libraries never become competing package orchestration interfaces. The approved
libcurl transport is used only for source acquisition and remains subject to
CBS's temporary-file, SHA-256, mirror, and cache gates.

## Consequences

### Benefits

- CBS can reuse mature implementations for difficult bounded mechanisms.
- The CBS codebase and executable can remain focused on package orchestration.
- There is one user-facing package engine and one policy implementation.
- The complete bootstrap and self-update lineage is explicit and testable.
- TCC remains the sole compiler root.

### Costs and risks

- Approved libraries enlarge the trusted Cix base and CBS attack surface.
- ABI and version compatibility become part of CBS release engineering.
- Every stage-zero library must itself have a non-circular bootstrap path.
- Stage comparison requires deterministic source, compiler, linker, metadata,
  and packaging policy.
- Careless library admission could gradually turn a small base into an opaque
  dependency tree.

## Rejected alternatives

### CBS builds its own prerequisites during genesis

Rejected because CBS cannot be used before a working CBS executable exists.
Pretending otherwise hides a dependency cycle.

### Keep a separate permanent bootstrap package manager

Rejected because it creates parallel implementations and two sources of truth.
Stage 0 is the normal CBS source built at the bootstrap boundary, not another
package engine.

### Use an ambient preinstalled CBS binary indefinitely

Rejected because it makes the Cix bootstrap depend on an opaque historical
artifact and prevents reconstruction from the documented seed.

### Fall back to external commands

Rejected because library failure must not change CBS into a shell orchestrator
or create a second implementation path.

### Permit GCC or Clang for stage 0

Rejected by the TCC invariant in ADR-0001. Library linkage changes no part of
the compiler rule.

## Open decisions

1. Which exact libraries are admitted to the initial trusted seed?
2. What is the repository-owned stage-zero build entry point and interface?
3. Which reproducibility properties must match between stages 1 and 2: installed
   tree, uncompressed package model, or complete `.cixpkg` bytes?
4. How are seed library provenance and versions recorded in CBS build metadata?
5. How is a base library safely transitioned from its seed build to its
   CBS-managed package?

## Acceptance criteria

This ADR can become Accepted when:

- the project approves the stage 0 -> stage 1 -> stage 2 ownership transition;
- the initial base libraries are named in focused decisions;
- the stage-zero build entry point is specified;
- the bootstrap has no CBS/library dependency cycle;
- the TCC-only compiler lineage is preserved; and
- the stage comparison and failure policy are specified.
