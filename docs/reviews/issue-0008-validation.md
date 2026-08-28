# Issue #8 validation: named sources and paired checksums

- Issue: `#8 Implement named sources with paired checksums`
- Date: 2026-08-28
- Result: Pass
- Implementation: the commit containing this review

## Decision and implementation

One source declaration owns its kind, unique name, ordered mirror URLs, and one
SHA-256 digest. Multiple `url` declarations are allowed before the single
required `sha256`; mirrors are alternate locations for identical bytes and do
not carry positional checksums.

`src/source.c` constructs the immutable `CbsSourceSet` directly from the
validated AST and implements SHA-256 internally. It invokes no checksum command
or crypto helper. `cbs_source_verify` hashes a fetched local file, clears any
prior verified state before checking, and records its path only when the digest
matches exactly.

Checksum failure is `CPDL-E5001` in the new source-preparation diagnostic class.
The message includes the declared source name, expected digest, and computed
digest. This separates input preparation failure from a recipe operation
failure while matching the existing CBS exit-status model.

`cbs_sources_apply_execution_context` is the sole bridge to `$source.NAME`. It
fails unless every source in the set has a verified path, then creates all
bindings together. A recipe therefore cannot observe one early source while a
later source is unverified or mismatched.

The main source remains distinguished structurally for later extraction into
`$src`; extra sources retain their names for explicit prepare operations.

## Acceptance proof

The fixture declares a main source with primary and mirror URLs plus an extra
source. Tests verify:

- mirror order and the single paired digest survive AST conversion;
- bindings cannot be applied before verification;
- verifying only the first source still exposes nothing;
- SHA-256 of `abc` matches the standard
  `ba7816bf...15ad` vector;
- deliberately wrong bytes fail with `CPDL-E5001`, source name `empty`, and
  both expected and computed digest prefixes;
- mismatch does not create a verified path;
- verifying the empty-file standard vector completes the set; and
- both named paths then interpolate through the normal runtime value resolver.

## Verification

`make clean && make && make test` passes with TCC and
`-std=c11 -Wall -Wextra -Werror -pedantic`. The named-source suite also passes
when rebuilt from source with TCC `-b` bounds instrumentation.

## Result

- Warning-clean TCC build: Pass
- Existing regressions: Pass
- Multiple ordered mirrors: Pass
- Internal SHA-256 known vectors: Pass
- Named mismatch diagnostic: Pass
- All-sources verification gate: Pass
- Runtime binding publication: Pass
- TCC bounds-instrumented suite: Pass

Issue #9 can derive dependency structure beside this source model without
changing source identity or verification.
