# Issue #130: complete non-executing recipe explanation

## Result

Implemented and tested in standalone CBS.

`cbs explain RECIPE.cbs --json` now exposes the validated package name,
version, release, and an explicit `architecture: null` (architecture is
selected by the build command, not CPDL). It also emits every declared source,
all source URLs, each SHA-256 digest, dependency groups grouped by dependency
kind, and the existing ordered phase operation counts.

The command remains non-executing. It parses and validates once, then serializes
the AST facts an embedder needs to compose a build environment, pre-populate a
source cache, and plan the artifact identity. No embedder needs a second CPDL
parser. The CLI test asserts the new metadata alongside the phase data.
