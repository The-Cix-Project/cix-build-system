# CBS and cixd integration contract

This contract applies to CBS v0.1.x releases. The executable, static library,
and public header must be taken from the same release tag; callers should
check `cbs --version` before relying on the CLI or public API surface.

This is the initial integration boundary for CPDL 0.1. cixd is the parent and
container owner; CBS is the build engine inside that container. CBS does not
need an HTTP client or an outbound daemon connection.

The supported first slice is process-based: cixd creates the container,
invokes `cbs`, and consumes its artifact and optional JSONL event stream. CBS
also ships a static `libcbs.a` and public header for deliberate direct
embedding, but it does not ship a shared object, plugin manifest, or dynamic
plugin loader.

## Before execution

cixd reads the recipe with `cbs explain RECIPE.cbs --json`. The result supplies
the package identity, source URLs and SHA-256 digests, dependency groups, and
ordered phase operation counts. cixd uses those facts to select the build
image, compose tools, resolve identity, and populate its cache.

`requires` item keywords are an open, embedder-defined vocabulary. CBS carries
each keyword through verbatim as the dependency `kind`; it does not silently
translate or reject an unknown kind as long as the dependency name is valid.
The `package` kind is the explicit spelling for a package identity reference,
while `library` remains a soname/library assertion.

## Build invocation

```text
cbs build RECIPE.cbs --arch ARCH --staged WORKSPACE --output ARTIFACT --cache CACHE
```

For process-level embedders, `cbs build` also accepts
`--finalize-command CMD`. CBS invokes `CMD WORKSPACE/dest` after the recipe
phases and before manifest/package creation; a non-zero exit rejects the build
and no artifact is written. The command is executed directly, without a shell.

CBS validates and plans the recipe, executes its phases, runs the embedder
finalization policy, computes the typed manifest, and writes/verifies CIXPKG v2.
The container has no network requirement: source fetching is cache-first.

Recipes can declare CBS-owned compiler tool policy:

```text
tools {
    compiler alias "cc"
}
```

CBS resolves the compiler once using the approved command path, materializes
the alias directory before phases begin, and prepends it to the child
PATH for the whole build. The resolved target and effective alias policy are
recorded in package provenance. A recipe `PATH` override is rejected; the
composer supplies the command-path policy.

Embedders can use `cbs_build_standalone_with_cache_policy()` to supply the
finalizer. They can register `CbsBuildEventSink` to receive versioned,
synchronous `build-begin`, source cache, `phase-begin`, `command-begin`,
`command-end`, `phase-end`, `artifact-finalized`, and `build-end` events while
the build is running. This is the
preferred integration path for cixd: it can forward events to terminal or web
UIs without scraping recipe output. The legacy phase-only callback in
`CbsExecutionContext` remains available for phase-only consumers.

CBS event callbacks are synchronous and may reject an event; CBS then fails the
build closed. Event strings and pointers are valid only for the callback
duration, and sequence numbers are monotonically increasing within a build.
The `cbs_build_event_jsonl()` and `cbs_build_event_human()` sinks provide
reusable output adapters, but presentation and transport remain cixd concerns.

For opt-in command retention, set `CBS_LOG_DIR` to an existing directory. CBS
writes one mode-0600 log per command and includes its path in command events;
the default is no command log and unchanged child output behavior.

Callers that need a persisted summary can initialize `CbsBuildReport`, use
`cbs_build_report_consume()` as the event sink, and serialize it after the
build with `cbs_build_report_write_json()`. The report aggregates phase and
command counts, cache decisions, source fetches, timings, observed resource
use, output sizes, artifact path, and final status.

Build callers may pass `--prune-policy FILE` (or the equivalent policy API)
after phase execution and before manifest generation. The policy file accepts
`strip-debug`, `drop-static-archives`, and `drop-libtool-archives`. Every
modification or removal is emitted as a structured prune event with its path,
rule, and recovered bytes; rules that match no paths emit an explicit
`prune-rule` event. Reports aggregate the prune file and byte totals.

Kernel-oriented embedders may pass `--firmware-root DIR`; recipes consume the
validated root as `${firmware}`. A recipe that uses that value without a
caller-supplied directory fails before execution. The embedding API exposes the
same `firmware_root` field on `CbsExecutionContext`. Curated kernel
configuration composition is available through `cbs_kconfig_merge()`: it
accepts only `CONFIG_*` assignments in `y`, `m`, or `n` form (including
`# CONFIG_* is not set`), applies fragment overrides deterministically, and
rejects arbitrary shell/configuration text.

## Ownership

cixd owns discovery, dependency/image composition, cache population, container
creation, repository authentication, publication, signing, transactions, and
rollback. CBS owns CPDL validation, phase execution, finalization callback
ordering, manifest/CIXPKG creation, verification, and extraction.

The first integration therefore needs no HTTP/OpenAPI client in CBS, second
recipe parser, CBS-owned namespace, or CBS-owned transaction layer.
