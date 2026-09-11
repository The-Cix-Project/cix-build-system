# CPDL 0.1 recipe-corpus coverage audit

This is the audit required before converting the Cix shell corpus. The
measured current corpus is 134 package revisions and 2,077 imperative shell
lines (mean 15.5 lines per package). The table records the observed construct,
its measured frequency where available, and the CPDL disposition.

| Construct | Occurrences | CPDL disposition |
|---|---:|---|
| `sed -i` source edits | present | `replace`/`insert` with exact cardinality; covered |
| Heredoc scratch C probes | present | `write` plus `run`; covered when the compiler/toolchain is declared |
| `install -D -m` | present | `mkdir`, `copy`, and `chmod`; covered |
| Generated wrappers on `PATH` | present | `write`, `chmod`, and explicit `PATH`; covered |
| `find ... -exec touch` | present | no general find/exec operation; native helper or recipe-specific explicit paths required |
| Timestamp ordering with `touch` | present | no ambient mtime primitive; native helper or explicit build-system operation required |
| Hand-rolled `if` refusals | 32 packages | `require`/`expect`; covered |
| `$(nproc)` | 84 packages | `$jobs`; covered |
| `CC=tcc` | 83 packages | structural `compiler "tcc"`; CBS now passes `CC=tcc` as a command-line make variable |
| GCC-style make dependency flags | observed in zstd | explicit `replace` removes unsupported `-MT/-MMD/-MP/-MF` flags for the TCC toolchain |
| `DESTDIR=` | 77 packages | `${dest}`; covered |
| Three-stage TCC bootstrap | present | existing isolated phases and byte-identity assertion; covered and gated by `make test` |
| `rm -rf .../share/man` | 35 packages | platform finalization policy; omit from migrated recipes |
| GCC source layout/private tool paths | present | unresolved in #144; source revision and compiler-path policy need correction |
| Kernel firmware/configuration helpers | present | unresolved in #144; executor capability/native helpers required |

## Named gaps

The remaining gaps are not reasons to add shell escape hatches. `find` and
mtime manipulation need either a narrowly reviewed native helper or explicit
recipe operations. Kernel firmware-root access and its kconfig operations are
executor capabilities. GCC's archive layout and private `COMPILER_PATH`/linker
requirements need a corrected source declaration and a structural toolchain
model.

The audit therefore estimates the easy conversion bulk, identifies the hard
tail, and prevents migration from silently translating unsupported shell into
ambient host behavior. It does not claim that the 134 packages have already
been converted.
