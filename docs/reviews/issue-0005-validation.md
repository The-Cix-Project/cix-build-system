# Issue #5 validation: source edits and assertions

- Issue: `#5 Implement source-edit and assertion operations with cardinality checks`
- Date: 2026-08-28
- Result: Pass
- Implementation: the commit containing this review

## Delivered boundary

The confined filesystem runtime now executes the CPDL 0.1 operations from
specification sections 4.6 and 4.7:

- `replace PATH { from VALUE to VALUE exactly N }`
- `insert PATH { after VALUE write VALUE exactly N }`
- `require file PATH { exists ... }`
- `require directory PATH { exists }`
- `require glob PATTERN { count N }`

All enter through `cbs_execute_edit_assertion` in `src/fs.c`. The implementation
reuses issue #4's path resolver, parent-link checks, glob matcher and walker,
and atomic writer. There is no second path resolver, alternate glob engine,
external patch tool, or assertion shell command.

## Edit transaction

`replace` and `insert` perform the following transaction:

1. resolve and confine the target path;
2. reject a final symlink or non-regular file;
3. read the original bytes and record its permission mode;
4. count non-overlapping literal matches;
5. compare the complete count with `exactly`;
6. allocate and construct the complete edited result only after the count
   succeeds; and
7. atomically replace the file using the issue #4 temporary-file-and-rename
   path while restoring the original permission mode.

If the expected count differs, CBS reports `CPDL-E4005` and performs no write.
Read, allocation-size, temporary-file, mode, close, or rename failures also
leave the original destination in place.

The edit implementation is byte-oriented. It does not use C string functions
to determine the input or output file length, so an existing file containing
embedded NUL bytes is preserved correctly around an edited match. CPDL match
and replacement values remain NUL-free as required by the language grammar.

## Assertion semantics

`require file` uses `lstat` and a no-follow open, so a final symlink is not
accepted as a regular file. Every `contains` property performs a literal byte
search against the same captured content.

`require directory` accepts only a real directory after the shared parent and
root confinement checks. A final symlink does not satisfy it.

`require glob` uses the one CBS glob implementation delivered for issue #4 and
compares the complete result count with the declared cardinality. Unlike
filesystem mutation selectors, an expected count of zero is valid and succeeds
when no path matches.

All type, content, and cardinality mismatches report `CPDL-E4005` rather than
being treated as filesystem mutation errors or silent success.

## AST value fidelity

The AST now retains the token kind for the second edit value, and generic
property nodes retain their source token kind. This preserves the normative
difference between quoted/interpolated values and block strings, whose bytes
must not undergo interpolation. Both edit operands and every `contains` value
therefore pass through the same `cbs_resolve_value` implementation used by
`run` and issue #4.

## Acceptance proof

The integration fixture successfully replaces two `old` sequences and inserts
after the resulting two `new` sequences, preserving the file mode. It then
requires two distinct content sequences, a directory, a two-result glob, and a
zero-result glob.

The negative suite starts with the final successful file and attempts:

- a replacement asserting exactly one match when two exist; and
- a replacement asserting exactly three matches when two exist.

Both calls return `CPDL-E4005`. After each call, the test verifies the original
16 bytes and `0640` mode are unchanged. This directly proves the ticket's zero,
one, and multiple-match safety objective rather than only checking a diagnostic
string.

Additional coverage verifies a cardinality mismatch for `require glob`, an
edit target outside the supplied roots, and a replacement within a binary file
containing an embedded NUL byte.

## Verification performed

The normal gate is:

```text
make clean
make
make test
```

All production and test translation units compile with TCC using:

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

The source-edit/assertion integration binary is also rebuilt from source with
TCC `-b` bounds instrumentation and run against the same fixture.

## Result

- Warning-clean TCC build: Pass
- Existing parser, execution, and filesystem regressions: Pass
- Successful replace/insert cardinality: Pass
- Zero and multiple mismatch without mutation: Pass
- File, directory, content, and glob assertions: Pass
- Explicit zero-result glob assertion: Pass
- Binary content preservation: Pass
- Root confinement: Pass
- TCC bounds-instrumented suite: Pass

Issue #6 can compose operation failure behavior around this single execution
boundary without changing edit or assertion semantics.
