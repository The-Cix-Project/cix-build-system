# Issue #122 validation: ELF runtime dependency observation

`cbs_observe_dependencies` now reads the ELF64 program and dynamic headers
directly. It maps `DT_STRTAB` through a `PT_LOAD` segment, validates the string
table and every `DT_NEEDED` offset, and calls the supplied observer once for
each library name. It rejects non-ELF, non-little-endian, truncated, malformed,
and unsupported input instead of invoking an external `readelf` or shell.

This remains deliberately separate from dependency resolution. A host or
daemon embedder can compare the observed names with the recipe's `runtime`
declarations and decide how to report or stage them; standalone CBS never
silently installs an observed library.

Validated by `tests/observe-test.c` against `/bin/ls`, including a real
`DT_NEEDED` libc entry, and against `/etc/hosts` as invalid non-ELF input.
The complete `make test` suite passes.
