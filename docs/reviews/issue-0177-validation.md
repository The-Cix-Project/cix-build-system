# Issue #177 validation: `replace` is literal-only, so six packages cannot strip `-Wl,--version-script=<path>` from a Makefile

## Decision

The narrow prefix-to-delimiter form, not a pattern language:

```cbs
replace "${src}/libmnl-1.0.5/src/Makefile" {
    from "-Wl,--version-script="
    until whitespace
    to ""
    exactly 1
}
```

With `until`, each match is the literal `from` plus every following byte up
to, but not including, the first delimiter or the end of the file. `until
whitespace` stops at a space, tab, CR, or LF; `until line` stops at CR or
LF. The delimiter is never part of the match, matches are non-overlapping
and left to right, and `exactly N` counts them exactly as before, so an edit
that matches nothing still fails loudly. `insert` has no `until`. The clause
is stored as a property of the `replace` node, so glob targets and `each`
expansion carry it unchanged.

The toolchain-aware alternative ("strip linker flags this compiler does not
support") remains open as a separate decision; it would need a per-compiler
flag table and is not required to convert the six packages.

## Acceptance criteria

1. Remove `-Wl,--version-script=<anything up to whitespace>` with no shell —
   `tests/fixtures/execution/edit-assert.cbs` strips two occurrences whose
   arguments differ (`libfoo.map` mid-line, `lib/bar.map` at end of line)
   and asserts the exact resulting bytes; `tests/edit-assert-test.c` covers
   an argument that runs to end of file with no trailing newline.
2. Still fails loudly on no match — the C test asserts `source edit expected
   2 matches but found 1` with the file untouched.
3. `recipes/package/libmnl` — the user manual shows the libmnl-shaped edit
   over a glob target; the recipe itself lives in the Cix tree and is not
   converted here.

Also covered: `until line` rewriting a whole `CFLAGS +=` line, two invalid
fixtures with `.expect` text (`until spaces`; `until` under `insert`), and the
valid fixture exercising the clause through the fuzz corpus.

## Docs

CPDL spec §4.6 grammar and semantics, keyword list; user manual §11.6;
`docs/cpdl-test-coverage.md`.
