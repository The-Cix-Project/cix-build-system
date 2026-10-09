# Issue #289 validation: the CIXPKG v2 spec omits the `m <key> <value>` manifest metadata line that CBS writes

## Decision

`docs/spec/cixpkg-1.0.md` now specifies the metadata line alongside the three
entry forms, and says where it sits:

```text
* metadata:      m key value\n
* regular file:  f mode 0 0 size digest path\n
* directory:     d mode 0 0 path\n
* symbolic link: l mode 0 0 target-hex path\n
```

The manifest is "zero or more metadata lines, then entries sorted by unique
path", because the old wording — "sorted by unique path" over three entry
types — described neither the line CBS writes nor its position.

No format or implementation change. Existing v2 artifacts already carry these
lines and are read correctly.

## Why the omission mattered

The document calls itself the reader/writer contract, and it told readers to
reject what CBS writes: `m` was not one of the three documented types, and the
spec says "an unknown type is corruption". A second implementation written from
this document would have refused CBS artifacts whose first manifest line is
`m license …`, and would have omitted the line itself, so the same input would
not produce the same bytes.

## Checked against the implementation, not inferred

Every claim in the new text was verified in code and then against a real
artifact:

- `src/manifest.c` writes `m license %s` **before** the entry loop.
- Both readers parse the form generically —
  `sscanf(line, "m %31s %4095[^\n]", key, value)` — and neither compares the
  key against a known set, so the "readers accept any key" requirement is
  what the code already does.
- The verify path additionally rejects an **empty value**, which the text now
  states.
- `license` is the only key CPDL 1.0 writes.

A recipe declaring `license "GPL-3.0-or-later"` produces an artifact that
verifies and reports `metadata license GPL-3.0-or-later`, confirming the
round trip.

The "readers accept any key" sentence is deliberately a requirement rather
than a description: it is what lets a later release record a new fact without
a format version, and it is already true of both readers.

## Deliberate wording choices

- "A writer emits its metadata lines before the first entry" is a writer
  requirement. The readers tolerate an `m` line anywhere, so it is not stated
  as something readers must enforce.
- The path-ordering requirement is now scoped to entries only.

## Acceptance criteria

1. The four line forms are specified, with the metadata key and value bounds
   the readers enforce.
2. A licensed artifact verifies and lists its manifest license — re-checked on
   v0.1.111.
3. `make -j1 test` unchanged: the CIXPKG round-trip and hostile-corpus tests
   already cover the format, and this is documentation.

## Still in the wrong document

`cpdl-1.0.md` §3.1 already said `license` is "carried into the artifact
manifest as an `m license <expression>` line". That stays — it is the right
place to describe the CPDL declaration's effect — and the binary format now
describes the line it produces.
