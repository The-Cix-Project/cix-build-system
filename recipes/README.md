# CBS recipe seed set

This directory contains CPDL recipes being migrated into CBS. It is separate
from the historical shell recipe corpus: `.cbs` files are the replacement
source of truth and must not invoke a shell interpreter.

## TCC

[`tcc.cbs`](tcc.cbs) is the first migration draft. It carries the exact pinned
TCC source commit, checksum, package identity, dependency declarations, and
basic configure/build/install flow from the legacy recipe.

The draft now builds successfully in CBS's local sandbox and emits a verified
`tcc-0.9.28rc-29-x86_64.cixpkg`. It is not yet release-equivalent to the legacy
recipe: that recipe also applies compatibility edits, runs compiler conformance
gates, and performs a three-stage bootstrap. Those behaviors are called out in
the CPDL file and must be translated into explicit operations and assertions
before this recipe can replace the legacy build in production.

## GCC

[`gcc.cbs`](gcc.cbs) is the next migration draft. It carries the GCC and GMP
source pins documented in ADR-0001 and the TCC-rooted dependency model. It is
validation-only until pinned MPFR/MPC sources and the complete GCC test and
bootstrap gates are added.
