# CPDL 1.0 recipe-corpus coverage audit (historical, 2026-09-18)

**Status: closed.** This audit was the prerequisite for converting the Cix
shell corpus. That conversion is complete: measured on 2026-10-09 the corpus
holds 1010 CPDL definitions across 181 distinct packages and no shell recipes,
with 1567 `.sh` package files in its history and none surviving.

The document is kept as the record of what the shell corpus contained and how
each construct was answered, because that reasoning is why the CPDL vocabulary
has the shape it does. It is not a plan, and the dispositions below describe
the language as of 2026-09-18.

Its measurements are from the audit: 134 package revisions and 2,077
imperative shell lines, mean 15.5 lines per package. Whether 134 counted
distinct packages or versioned files is not recoverable from the figure, so it
cannot be compared directly with the 181 packages measured today. The table
records the observed construct, its measured frequency where available, and the
CPDL disposition.

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
| Three-stage TCC bootstrap | present in retired migration findings | retained as issue history; not a shipped recipe fixture |
| `rm -rf .../share/man` | 35 packages | platform finalization policy; omit from migrated recipes |
| GCC source layout/private tool paths | present | `toolchain "gcc"` with a declared reason plus an explicit `COMPILER_PATH`; covered |
| Kernel firmware/configuration helpers | present | `${firmware}` with `--firmware-root` and `require config` cover firmware-root access and kconfig assertions; the remaining need is executor capability declaration |
| `for X in <literal list>` with a compound body | 38 loops, 15 packages | `each` (CPDL 1.0 §4.8, ADR-0035); covered |
| `sed` stripping a flag and its argument | 7 occurrences, 6 packages | `replace ... until whitespace` or `until line` (§4.6); covered |
| Multiarch shared-library search before `cp -a` | 4 packages | `stage library ... into` (§4.9, ADR-0036); covered |
| Lowercase autoconf/libtool cache variables | 10 packages | `env` accepts portable names in either case; covered |

## Named gaps, and how they ended

Every gap this audit recorded is closed. None was closed by adding a shell
escape hatch, which was the point of writing it down.

- **Language gaps** — iteration over a literal list, prefix-to-delimiter edits,
  shared-library staging, and lowercase environment names: all implemented on
  2026-09-18 (`each`, `replace … until`, `stage library`, portable-case `env`).
- **GCC's toolchain model** — the audit asked for "a corrected source
  declaration and a structural toolchain model". `gcc` is converted and
  validates, declaring `toolchain "gcc"` with a reason and handling the private
  `COMPILER_PATH` explicitly, which is that model in use.
- **Executor capability declaration** — recorded as open. 272 recipes in the
  corpus now declare `capability`.
- **Kernel firmware and configuration** — `${firmware}` with `--firmware-root`
  and `require config`; the converted `kernel` recipe asserts `y`/`m`/`absent`
  states directly.
- **`find`/`-exec` and ambient mtime** — the two the audit judged genuinely
  unsupported. Neither acquired an operation and no native helper was approved;
  recipes use explicit paths or the build system's own facility instead.

The audit's purpose was to estimate the easy conversion bulk, identify the hard
tail, and keep migration from silently translating unsupported shell into
ambient host behaviour. Judged against the outcome it succeeded: the two
constructs it named as genuinely unsupported — general `find`/`-exec` and
ambient mtime manipulation — never acquired a shell escape hatch, and no native
helper was approved to serve them (see the
[native helper registry](native-helpers.md), still empty).
