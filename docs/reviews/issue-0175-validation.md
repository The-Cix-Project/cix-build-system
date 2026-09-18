# Issue #175 validation: `require file` fails on a symlink and reports "does not exist", which is not what happened

## Cause

`require_path` in `src/fs.c` funnelled path resolution, parent-safety,
`lstat`, and type checks into one `failed:` label whose only sentence was
`does not exist`. A soname link such as `usr/lib/liblzma.so.5` is a symbolic
link, `require file` matches regular files only, and the message blamed the
install for producing nothing.

## Fix

- Every failure names what was found. The messages are now
  ``required file `P` is a symbolic link; require file matches regular files only``,
  ``… is a directory; …``, ``required directory `P` is a regular file; require
  directory matches directories only``, ``… has a parent that is a symbolic
  link or not a directory``, ``… cannot be read: <strerror>``, and `does not
  exist` only when `lstat` reports `ENOENT`. `errno` is captured before any
  formatting.
- `require symlink PATH { exists [target TEXT] }` asserts on a link directly.
  It uses `lstat`, never follows the link (a dangling link satisfies
  `exists`), and `target` compares the link text literally after `${...}`
  substitution — the same resolution the `symlink` operation applies to its
  target, so the two round-trip. The property is spelled `target`, not `to`,
  because `to` already means "the link's location" in `symlink … to …`.
- The validator now enforces per-kind properties: `directory` accepts only
  `exists`, `symlink` accepts only `target`, `file` accepts `contains`,
  `same_as`, and `nonempty`. Previously `require directory X { exists
  contains "y" }` parsed, validated, and silently ignored `contains`.

## Acceptance criteria

1. Symlink under `require file` → "is a symbolic link" — `tests/edit-assert-test.c`.
2. Directory under `require file` → "is a directory" — same test.
3. Absent path → "does not exist" — same test, for `file` and `symlink`.
4. `require directory` on a symlink and on a regular file → symmetric
   messages — same test.

Also covered: a parent that is a regular file, `target` mismatch (``points to
`input.bin`, expected `wrong` ``), `target` match, a dangling link, and the
fixture `tests/fixtures/execution/edit-assert.cbs` executing `symlink` then
`require symlink` through the runtime path. Three invalid fixtures carry
`.expect` files, and `tests/parser-validation.sh` now asserts that text when
an `.expect` file exists beside an invalid fixture.

## Docs

CPDL spec §4.7 (grammar, semantics, keyword `target`), user manual §6
assertions, `docs/cpdl-test-coverage.md`.
