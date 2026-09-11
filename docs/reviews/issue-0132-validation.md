# Issue #132: CBS/cixd integration contract

## Result

Resolved as the initial integration contract.

cixd reads `cbs explain RECIPE.cbs --json`, composes the build image,
pre-populates the cache, creates the container, and invokes `cbs build` inside
it. CBS validates, executes phases, applies the embedder finalizer, emits phase
events through its API callback, and writes/verifies CIXPKG v2. cixd then owns
publication, authentication, signing, graph resolution, and transactions.

The first slice deliberately requires no HTTP/OpenAPI client in CBS and no
second CPDL parser. The complete ownership split and command contract are in
`docs/integration-contract.md`.
