# Issue #294 validation: a plain-text source whose first line looks like mtree is refused as a malformed archive

## Cause

`cbs_archive_probe()` computed the header result and discarded it:

```c
    header_result = archive_read_next_header(reader, &entry);
    format = archive_format_name(reader);
    if (format == NULL || format[0] == '\0')
        result = 0;
    else if (supported_format(format))
        result = 1;
    else
        result = -1;
    (void)header_result;
```

libarchive sets the format name when it *guesses* a format, before knowing
whether the stream parses. Its mtree reader claims any text whose first line
reads `word key=value`, and `contents`, `mode`, `size`, `uid` and `gid` are all
mtree keywords. Such a file therefore reached the final branch with a non-empty,
unsupported format name and probed as `-1`, "recognized but invalid archive",
which source preparation turns into a hard `CPDL-E6001`.

A file whose header read failed is not a recognized archive. It is a file
libarchive guessed wrong about.

## Decision

Only the unsupported-format branch consults the header result:

```c
    else
        result = header_result == ARCHIVE_OK ? -1 : 0;
```

An unsupported format CBS can actually parse stays a deliberate refusal — a
real cpio is not silently materialized as an opaque blob. An unsupported format
whose header does not even read is a misdetection, and the file is ordinary.

The supported-format branch is deliberately untouched. A corrupt tar still
probes as an archive and fails during extraction, where the diagnostic can name
the member and the rule; routing it through the probe would produce a worse
error.

## Evidence

Each row is a source file with that exact content, declared with its SHA-256
and consumed by `materialize $source.m to "${build}/out"`:

| content | v0.1.113 | this change |
| --- | --- | --- |
| `CONFIG_X=y` | materialized | materialized |
| `hello world` | materialized | materialized |
| `#!/bin/sh` … | materialized | materialized |
| `cfg contents` | **`CPDL-E6001`** | materialized |
| `name mode=0644` | **`CPDL-E6001`** | materialized |
| `name size=12` | **`CPDL-E6001`** | materialized |
| `name uid=0 gid=0` | **`CPDL-E6001`** | materialized |
| `{"a":1}` | **`CPDL-E6001`** | materialized |

Probe results for real archives are unchanged, measured against both binaries:
a valid tar.gz probes 1, a cpio probes -1, a zip with a truncated header probes
1 and is refused by extraction. The fix is confined to the misdetection case.

**Not a production breakage.** The corpus's 3219 `materialize` uses all name
package artifacts, and no `.json`, `.conf`, `.cfg`, `.ini`, `.toml` or `.yaml`
source is declared anywhere in it. This was latent. It is worth fixing because
CPDL 1.0 §4.5 names configuration fragments as `materialize`'s purpose, which
is the class that was broken.

## Acceptance criteria

1. **Text probes as an ordinary file** — `tests/archive-test.c` asserts
   `cbs_archive_probe` returns 0 for all seven shapes above, including the
   five that previously failed.
2. **A supported archive still probes as an archive** — asserted 1.
3. **An unsupported but parseable format is still refused** — a cpio archive
   written by the test asserts -1, so the fix cannot have been made by calling
   every failed guess a file.

Verified by restoring `result = -1` in a scratch build: the new assertions fail
there and pass with the fix.

## A phantom worth recording

While testing the refusals I believed I had found a third defect: a truncated
`tar.gz` appearing to extract nothing and succeed. Both halves were wrong. The
archive was 175 bytes, so truncating it to 300 produced a byte-identical file,
and the apparently empty workspace came from a stale generated recipe still
pointing at a different fixture whose refusal I had redirected away. CBS
extracted all three members correctly. Recorded because the mistake was mine
and an un-recorded phantom tends to be rediscovered.

## Docs

CPDL 1.0 §4.5 now states what counts as an archive for `materialize` and why
the distinction exists; `docs/cpdl-test-coverage.md` gains a probing row.
