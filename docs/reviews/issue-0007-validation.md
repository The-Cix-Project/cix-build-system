# Issue #7 validation: canonical package identity

- Issue: `#7 Implement package identity: name, upstream version, Cix release, architecture`
- Date: 2026-08-28
- Result: Pass
- Implementation: the commit containing this review

## Delivered boundary

`src/identity.c` is the one construction and serialization boundary for package
identity. `CbsPackageIdentity` contains exactly:

- package name from CPDL;
- upstream version from CPDL;
- positive Cix release from CPDL; and
- target architecture supplied by CBS.

The identity retains borrowed references to the validated AST and CBS policy
value. It is immutable after construction and does not duplicate or reinterpret
recipe declarations.

## Architecture decision

Issue #7 and ADR-0001 require architecture to be supplied by CBS. The initial
CPDL specification still described recipe-level exact and `any` declarations,
which contradicted that requirement.

The normative CPDL 0.1 grammar now excludes architecture declarations. The
lexer and parser continue to recognize the reserved keywords so an attempted
declaration receives the precise validation error `CPDL-E3006` rather than an
unknown-token accident. `architecture any` remains reserved for a future
architecture-independent-package decision; it has no CPDL 0.1 semantics.

CBS target architecture values must match a deliberately narrow canonical
form: a lowercase ASCII letter or digit followed by lowercase letters, digits,
or underscores. The reserved value `any`, uppercase aliases, separators, path
components, and empty values are rejected before identity construction.

The former valid exact-architecture fixture moved to the invalid suite, and the
complete valid fixture no longer declares architecture.

## One canonical representation

`cbs_identity_from_document` is the only function that combines recipe fields
with CBS architecture policy. The resulting tuple has one canonical text:

```text
name-version-release-architecture
```

For the ticket's reference identity this is:

```text
gcc-16.2.0-11-x86_64
```

Consumers derive from that function and `cbs_identity_string`:

- `cbs_identity_artifact_filename` appends `.cixpkg` to the canonical text;
- `cbs_identity_digest_metadata` emits the exact bytes
  `identity=CANONICAL\n` for inclusion in artifact metadata and its digest; and
- `cbs_identity_apply_execution_context` supplies `$name`, `$version`,
  `$release`, and `$arch` from the same tuple.

No consumer independently concatenates the four source fields. Future manifest
and artifact implementations must accept `CbsPackageIdentity` or its canonical
metadata rather than reconstructing identity from the AST.

## Validation and safety

The existing validator remains the authority for package-name, version, release,
uniqueness, and canonical declaration-order rules. Identity construction rejects
an invalid document shape, missing identity fields, non-positive release, a
recipe architecture node, or an invalid CBS architecture even if called without
the normal validation prerequisite.

Package names and versions already exclude `/` and whitespace where required,
and CBS architecture excludes path punctuation. The derived artifact filename
therefore remains one filename component.

## Acceptance proof

The identity integration test parses and validates:

```text
package "gcc" {
    version "16.2.0"
    release 11
}
```

With CBS target `x86_64`, it verifies byte-for-byte equality for:

- canonical identity: `gcc-16.2.0-11-x86_64`;
- artifact filename: `gcc-16.2.0-11-x86_64.cixpkg`;
- digest metadata: `identity=gcc-16.2.0-11-x86_64\n`; and
- runtime interpolation of `${name}-${version}-${release}-${arch}`.

It also verifies rejection of `any`, uppercase `X86_64`, and a path-like target
architecture. Parser validation separately proves recipe-level `architecture
any` is invalid.

## Verification performed

The normal gate is:

```text
make clean
make
make test
```

All production and test code compiles with TCC using:

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

The identity test is also rebuilt from source with TCC `-b` bounds
instrumentation and run against the canonical fixture.

## Result

- Warning-clean TCC build: Pass
- Existing runtime regressions: Pass
- Recipe identity validation: Pass
- CBS-supplied architecture enforcement: Pass
- Canonical identity text: Pass
- Artifact filename derivation: Pass
- Digest metadata derivation: Pass
- Runtime context derivation: Pass
- TCC bounds-instrumented identity suite: Pass

Issue #8 can attach named sources and checksums to this package identity without
introducing another package-key representation.
