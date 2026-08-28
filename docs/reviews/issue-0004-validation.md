# Issue #4 validation: CPDL filesystem vocabulary

- Issue: `#4 Implement the CPDL filesystem vocabulary`
- Date: 2026-08-28
- Result: Pass
- Implementation: the commit containing this review

## Delivered boundary

`src/fs.c` is the single production implementation for the CPDL 0.1
filesystem operations defined in specification section 4.4:

- `mkdir`
- `copy`
- `move`
- `remove`
- `symlink`
- `write`
- `chmod`

Archive extraction remains outside this change. Issues #13 and #14 own its
format policy and implementation, so issue #4 does not introduce a provisional
extractor or a second execution path.

The implementation uses C runtime and kernel filesystem interfaces directly.
It does not invoke `mkdir`, `cp`, `mv`, `rm`, `ln`, `chmod`, a shell, or any
other userspace utility.

## One value and operation path

The value resolver originally introduced for `run` is now the shared
`cbs_resolve_value` function. Filesystem paths, write content, command arguments,
and command-local environment values therefore use one interpolation
implementation. Named source values are represented explicitly by
`CbsNamedSource` entries in the immutable execution context.

All filesystem nodes enter through `cbs_execute_filesystem`. There is no
operation-specific CLI adapter, shell fallback, or alternate test-only
implementation.

## Path confinement

Before an operation touches the filesystem, CBS:

1. resolves immutable CBS values;
2. joins relative paths to the operation's directory context;
3. normalizes repeated separators and `.` components;
4. rejects `..` resolution that lands outside `$src`, `$build`, and `$dest`;
5. verifies the selected root is an absolute, canonical, real directory rather
   than a symlink; and
6. walks every existing parent with `lstat`, rejecting a non-directory or
   symlink component.

The runtime repeats these checks for every glob result immediately before its
operation. Globbing never descends through a symlinked directory.

The confinement test attempts both `${dest}/../outside/escaped` and a write
through `${dest}/linked-parent/escaped`, where `linked-parent` is a symlink to
an outside directory. Both return `CPDL-E4004`, and the outside file remains
absent.

## Defined filesystem semantics

### Directories

`mkdir` creates missing components, accepts existing directories, rejects other
types, and applies its final mode. The test verifies an explicit `0700` mode.

### Copy and move

Regular-file copy preserves bytes and permission bits. A source symlink is
copied as a symlink with its exact target bytes. Directory copy is rejected;
there is no recursive copy in CPDL 0.1. An existing regular file is replaced,
while a destination symlink is never followed.

Multiple matches require an existing directory destination. Matches are sorted
by bytewise path order. Move uses `rename` and therefore cannot silently degrade
to copy-and-delete on `EXDEV`.

The specification now states ownership behavior explicitly: copied files are
owned by the CBS build identity. Staged ownership is assigned later by package
policy rather than copied from an input inode.

### Removal

Normal removal handles files, symlinks, and empty directories. A non-empty
directory fails unless `tree` is present. Tree removal uses `lstat` and does not
traverse directory symlinks. Zero-match globs fail.

### Symlinks

`symlink` writes the target bytes without canonicalization. The link path is
confined independently, and an existing destination fails.

### Atomic write

`write` creates a temporary file beside its destination, writes the complete
byte value, applies the requested or default mode, closes it, and renames it
over the destination. Failure removes the temporary file. The suite verifies
atomic replacement content and mode.

### Mode changes

`chmod` applies permission bits to regular files and directories. It refuses a
final symlink rather than following it; the test confirms the symlink target's
mode remains unchanged.

## Glob semantics

CBS implements matching internally for `?`, `*`, complete-component `**`, byte
classes, negated byte classes, ranges, and backslash quoting. Matching is
bytewise and case-sensitive. The walker includes hidden entries, sorts results,
and never traverses symlinked directories. Static syntax validation remains in
the existing validator, while runtime selection is owned by the filesystem
executor.

## Verification performed

The normal gate was run from a clean tree:

```text
make clean
make
make test
```

Production and test code compiled with TCC under:

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

The full gate covers:

- 49 existing lexer, parser, AST, and validator cases;
- direct `execve` argument/environment regression coverage;
- all seven section 4.4 filesystem operations;
- default and explicit modes;
- existing destination replacement;
- exact source-symlink copying;
- named source resolution;
- single and multiple glob selection;
- zero-match rejection;
- directory-copy rejection;
- non-tree and tree removal;
- final-symlink `chmod` rejection;
- lexical traversal rejection; and
- symlink-parent traversal rejection.

## Result

- Warning-clean TCC build: Pass
- Existing regression suite: Pass
- Filesystem operation suite: Pass
- Root-confinement suite: Pass
- TCC bounds-instrumented filesystem suite: Pass
- External filesystem utility audit: Pass
- One filesystem implementation: Pass

Issue #5 can build atomic source edits and assertions on this same confined
path and filesystem execution boundary.
