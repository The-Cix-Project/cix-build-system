# Issue #178 validation: No way to take a shared library out of the build sandbox when its directory differs by host

## Decision

The second form the report offered — an operation that owns the whole job —
recorded as ADR-0036:

```cbs
stage library "libresolv.so.2" into "${dest}/usr/lib"
```

CBS searches `/usr/lib/TRIPLET`, `/lib/TRIPLET`, `/usr/lib`, `/lib`,
`/usr/lib64`, `/lib64` in that order (`TRIPLET` from the existing
`${triplet}` mapping) and copies the first regular file or symbolic link it
finds. The destination is created when absent and left alone when present.
The name must be a bare file name, checked by the validator and again at
runtime after interpolation. This is the one operation that reads outside
the confined roots; the candidate list is the build image's library layout
and never a recipe value, which is why it is an operation rather than a
`copy` variant.

## Acceptance criteria

1. Copy a named shared library without naming its directory —
   `tests/fixtures/execution/filesystem.cbs` stages `libc.so.6` into
   `${dest}/usr/lib` with no prior `mkdir`; `tests/fs-test.c` checks the
   directory was created and the staged entry has the host copy's type,
   mode, and size.
2. Failure names the library — the C test asserts ``library is not in the
   build sandbox (searched …); declare the build dependency that provides
   it`` for a library no directory holds, and the bare-name rejection for
   `../etc/passwd`. Naming *which* declared dependency should have provided
   the library needs a library-to-package database CBS does not have, so
   the message names the library and every directory searched instead.
3. Mode and symlink-ness preserved — the copy goes through `copy_one`,
   which `filesystem.cbs` already covers for a symlink (`a-link`) and for
   mode preservation; a soname link is shipped as a link with its target
   text unchanged.
4. `recipes/package/mtr` — the user manual §6 shows the mtr/coreutils shape
   (`each` over the libraries, `stage library` per item); the recipe lives
   in the Cix tree and is not converted here.

Also: the #176 manual example that used `copy "/usr/lib/…"` (which fails
confinement at runtime) now uses `stage library`; two invalid fixtures carry
`.expect` text; the valid fixture exercises the operation through the fuzz
corpus.

## Docs

CPDL spec §4.1 grammar, §4.9, keyword list; ADR-0036; user manual §6;
`docs/cpdl-test-coverage.md`.
