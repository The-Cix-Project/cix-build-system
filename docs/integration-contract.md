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
finalizer. They can register `CbsBuildEventSink` to receive versioned,
synchronous `build-begin`, source cache, `phase-begin`, `command-begin`,
`command-end`, `phase-end`, `artifact-finalized`, and `build-end` events while
the build is running. This is the
preferred integration path for cixd: it can forward events to terminal or web
UIs without scraping recipe output. `CbsPhaseEvent` remains available as a
compatibility callback for phase-only consumers.

CBS event callbacks are synchronous and may reject an event; CBS then fails the
build closed. Event strings and pointers are valid only for the callback
duration, and sequence numbers are monotonically increasing within a build.
The `cbs_build_event_jsonl()` and `cbs_build_event_human()` sinks provide
reusable output adapters, but presentation and transport remain cixd concerns.

For opt-in command retention, set `CBS_LOG_DIR` to an existing directory. CBS
writes one mode-0600 log per command and includes its path in command events;
the default is no command log and unchanged child output behavior.

## Ownership

cixd owns discovery, dependency/image composition, cache population, container
creation, repository authentication, publication, signing, transactions, and
rollback. CBS owns CPDL validation, phase execution, finalization callback
ordering, manifest/CIXPKG creation, verification, and extraction.

The first integration therefore needs no HTTP/OpenAPI client in CBS, second
recipe parser, CBS-owned namespace, or CBS-owned transaction layer.
