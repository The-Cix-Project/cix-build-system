# Issue #135 validation: determinism is asserted everywhere and measured nowhere

Issue #135 was closed on 2026-09-11. ADR-0033 names the byte-identical
reproducibility gate as the second CIXPKG cutover gate; this record is the
measurement the title asked for.

Three layers now enforce it:

1. `tests/repro-test.c` asserts the comparison primitive itself: equal bytes
   pass, and a changed byte, a shorter file, a longer file, and a missing file
   each fail. It then builds `tests/fixtures/repro-tree.cbs` (nested
   directories, an empty directory, 0755/0644/0600 files, relative and
   absolute symlinks) twice through `cbs_build_standalone_with_cache` in
   separate workspaces and requires identical artifacts. The previous version
   of this test only compared two equal files.
2. `tests/cli-build-test.sh` builds `standalone-smoke.cbs` twice through the
   CLI with separate workspaces and caches and requires `cmp` to pass.
3. `tests/upstream-smoke-test.sh` builds the real `zstd` 1.5.4 package twice
   with TCC into separate staged trees from one verified source cache and
   requires byte-identical artifacts. On the validation host both builds
   produced a 267,063-byte `.cixpkg` containing the static and shared
   libraries, versioned `.so` symlinks, headers, and pkg-config file, and
   `cmp` reported no difference.

Determinism comes from the format and pipeline, not from the test: manifests
are sorted by unique path with normalized `uid=0 gid=0` and carry no
timestamps, payload bytes follow manifest order, compression is fixed zstd
level 19, identity derives from the recipe and architecture, and the build
identifier (time and pid) is used only for events and logs. Reproducibility of
compiler output remains a recipe and toolchain property; the zstd gate shows
the TCC path holds.
