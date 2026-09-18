# Issue #174 validation: cbs refuses the bzip2 1.0.8 tarball as "unsafe archive", and the message names neither an entry nor a rule

## Cause

`bzip2-1.0.8.tar.gz` (sha256 `ab5a0317…2269`) contains 56 regular files and
no directory members at all (`tar -tf` lists nothing ending in `/`). The
extractor only created directories for `AE_IFDIR` members, so the first
`open()` under `bzip2-1.0.8/` failed with `ENOENT`. libarchive had reported no
error, and the fallback text for "libarchive said nothing" was `unsafe
archive`. The report's hypothesis was correct.

## Fix

`src/archive.c`:

- Parents missing from the archive are created (mode 0755) before any file,
  symlink, or hard link member is written, in both extraction passes.
- Directory members are created as 0755 and their declared mode and mtime are
  applied after every member has been written, so a read-only directory
  listed before its contents, or a directory listed after its contents, both
  extract correctly.
- Every failure site records the member and either the policy rule or the
  failed operation with its `errno`. The `CPDL-E6001` message is now
  ``source `NAME`: member "PATH": rejected: RULE`` for policy,
  ``source `NAME`: member "PATH": OPERATION: STRERROR`` for filesystem
  failures, and names the detected format when the format check fails. The
  text `unsafe archive` no longer exists.
- The reader is no longer dereferenced when its allocation fails.

## Evidence

- `tests/archive-test.c` extracts a synthetic directory-less archive (file
  under two implicit parents, symlink-only directory, a 0700 directory member
  after its contents with mtime 500, a 0555 directory member before its
  contents with mtime 600) and checks every mode and mtime. It captures
  stderr and asserts the message text for an empty archive, `..` traversal,
  an absolute symlink target, a climbing symlink target, and a character
  device member.
- The real tarball: a recipe declaring `bzip2-1.0.8.tar.gz` failed before the
  fix with the reported message and, after it, fetched, extracted 56 files
  under `bzip2-1.0.8/` (0755), passed `require file`, and produced a verified
  `bzip2-1.0.8-1-x86_64` CIXPKG.
- `make -o upstream-test test` passes.

## Not changed

Recipes still cannot inspect an archive that CBS refuses, since extraction
precedes every phase; the named member and rule are what make that
unnecessary in the common case.
