# CBS and cixd integration contract

This is the initial integration boundary for CPDL 0.1. cixd is the parent and
container owner; CBS is the build engine inside that container. CBS does not
need an HTTP client or an outbound daemon connection.

## Before execution

cixd reads the recipe with `cbs explain RECIPE.cbs --json`. The result supplies
the package identity, source URLs and SHA-256 digests, dependency groups, and
ordered phase operation counts. cixd uses those facts to select the build
image, compose tools, resolve identity, and populate its cache.

## Build invocation

```text
cbs build RECIPE.cbs --arch ARCH --staged WORKSPACE --output ARTIFACT --cache CACHE
```

CBS validates and plans the recipe, executes its phases, runs the embedder
finalization policy, computes the typed manifest, and writes/verifies CIXPKG v2.
The container has no network requirement: source fetching is cache-first.

Embedders can use `cbs_build_standalone_with_cache_policy()` to supply the
finalizer. They can also register `CbsPhaseEvent` to receive `phase-begin` and
`phase-end` events, with end status zero or one, without parsing recipe output.

## Ownership

cixd owns discovery, dependency/image composition, cache population, container
creation, repository authentication, publication, signing, transactions, and
rollback. CBS owns CPDL validation, phase execution, finalization callback
ordering, manifest/CIXPKG creation, verification, and extraction.

The first integration therefore needs no HTTP/OpenAPI client in CBS, second
recipe parser, CBS-owned namespace, or CBS-owned transaction layer.
