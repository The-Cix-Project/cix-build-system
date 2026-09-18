# Issue #128 validation: CIXPKG cannot represent a symlink, an empty directory, or file ownership

Issue #128 was closed on 2026-09-11 when typed entries landed. ADR-0033 names
its typed-tree round trip as the first CIXPKG cutover gate; this record makes
that gate exact rather than spot-checked.

`tests/typed-package-test.c` stages a tree containing an empty directory, a
0755 regular file, an absolute symlink, and a relative symlink, then writes,
verifies, and extracts the CIXPKG v2 artifact. The round trip is now exact
rather than spot-checked: the extracted tree is re-manifested and must compare
byte-identical to the original manifest, which covers every path, type, mode,
size, digest, and link target in one assertion, and re-packaging the extracted
tree must reproduce the original `.cixpkg` bytes. The existing checks for
preserved modes, preserved link targets, empty directories, climbing-link
rejection, and setuid rejection remain in place.

`tests/package-test.c` continues to cover the build-pipeline path (recipe to
artifact to extracted file) and the corruption gates.

Scope: CBS is the only CIXPKG implementation (#127); an independent reader
belongs to the cixd/repository boundary (#132) and is not part of this gate.
