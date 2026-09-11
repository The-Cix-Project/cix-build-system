# Issue #124 validation: CBS host libraries

The CBS executable links `libarchive` and `libzstd`, and the repository now
contains source-built recipes for both libraries. The recipes pin upstream
source bytes, use the TCC build policy, install into the staged destination,
and gate their expected build outputs before installation.

`recipes/zstd.cbs` builds the zstd library directly from its `lib` Makefile.
`recipes/libarchive.cbs` builds the shared libarchive library with optional
backends disabled unless they are explicitly needed by CBS. This removes the
previous “the host happens to have development packages” assumption; a Cix
host can compose these declared packages before building CBS.
