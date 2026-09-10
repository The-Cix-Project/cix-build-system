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

[`gcc.cbs`](gcc.cbs) is the next migration draft. It now carries the GCC,
GMP, MPFR, and MPC source pins, executable prerequisite extraction, and the
TCC-rooted dependency model. The current probe reaches the fetched GCC source
tree but stops because this pinned archive has no top-level `configure` script;
the recipe therefore does not yet claim a complete GCC build or bootstrap.
That source-layout issue must be resolved from the declared source itself
before porting the remaining GCC-specific gates and install policy.

## Cix builder

[`cix.cbs`](cix.cbs) translates the Cix builder's source identity, TCC-rooted
build, selftest, #296 mutation gate, staged binaries, and web assets. CPDL 0.1
can now express the legacy build-image and capability policy. The recipe still
does not claim to grant those controls in standalone mode; the validated
metadata is exposed to the execution plan for the daemon/orchestrator.

## Kernel

[`kernel.cbs`](kernel.cbs) is the non-building migration draft for the Linux
kernel recipe. It preserves the pinned kernel and configuration sources,
declares the kernel-builder image, capability, and explicit GCC exception,
materializes the verified configuration, runs native kconfig operations, and
checks the required built-in, modular, and firmware inputs. It remains
non-building until the executor can provide the kernel-builder image's
firmware root and the remaining kernel-specific helper operations.

## squashfs-tools

[`squashfs-tools.cbs`](squashfs-tools.cbs) carries the XZ-only build, the
TCC compatibility flags, the single-reader setting, the image write/read-back
gate, and the four installed tools. `libgcc_s.so.1`, `liblzma.so`, and libc are
declared runtime requirements; CBS does not copy libraries from absolute host
paths into the package.
