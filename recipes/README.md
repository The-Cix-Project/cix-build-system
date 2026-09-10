# CBS recipe seed set

This directory contains CPDL recipes being migrated into CBS. It is separate
from the historical shell recipe corpus: `.cbs` files are the replacement
source of truth and must not invoke a shell interpreter.

## TCC

[`tcc.cbs`](tcc.cbs) is the first migration draft. It carries the exact pinned
TCC source commit, checksum, package identity, dependency declarations,
compatibility edits, executable gates, and configure/build/install flow from
the legacy recipe.

The draft now builds successfully in CBS's local sandbox and emits a verified
`tcc-0.9.28rc-29-x86_64.cixpkg`. The recipe now performs the compatibility
edits, executable gates, and three-stage self-bootstrap with a byte-identity
gate. The libc triplet is computed from the target architecture by CBS and is
used in every bootstrap configure pass. Architectures without a registered
mapping resolve empty and remain unsupported until their mapping is added. The
TCC recipe is deliberately restricted to `x86_64`, matching its measured
ELF64 and `R_X86_64` compatibility gates.

The `.eh_frame` relocation inspection is implemented as a native ELF64 helper,
and the library-path check is an isolated `-lc` link probe using the computed
triplet. No host library is installed or modified by the recipe.

## GCC

[`gcc.cbs`](gcc.cbs) is the next migration draft. It carries the GCC and GMP
source pins documented in ADR-0001 and the TCC-rooted dependency model. It is
validation-only until pinned MPFR/MPC sources and the complete GCC test and
bootstrap gates are added.
