# cixd embedding guide

CBS has two supported integration shapes. Choose one deliberately for each
deployment.

## Process boundary: the first slice

The cixd parent owns the container, image, cache, authorization, and
publication. Inside that container it invokes the matching CBS executable:

```sh
cbs explain RECIPE.cbs --json
cbs build RECIPE.cbs \
    --arch ARCH \
    --staged WORKSPACE \
    --output ARTIFACT \
    --cache CACHE \
    --events jsonl \
    --report REPORT.json
```

Use `explain --json` before execution to select an image and compose the
declared dependencies. Forward JSONL events to the interactive UI and retain
the final report for build history. Use `--diagnostics=jsonl` on commands that
can fail; diagnostics are machine-readable on stderr and do not replace the
event stream.

After a successful build, verify the artifact before publication:

```sh
cbs verify ARTIFACT.cixpkg
cbs list ARTIFACT.cixpkg --json
```

The artifact's `build_fingerprint` provenance is the value consumers should
store with the cache record. A precomputed `cbs fingerprint` key must use the
same architecture, command/library roots, input bindings, and complete tool
identity set as the build.

## In-process boundary: deliberate embedding

The repository installs `libcbs.a` and `cbs/cbs.h`; it does not install a
shared object or plugin loader. Link the static archive with the matching
public header and check the version guards before using the API:

```c
if (cbs_api_version() != CBS_API_VERSION ||
    cbs_abi_version() != CBS_ABI_VERSION ||
    cbs_execution_context_size() != sizeof(CbsExecutionContext)) {
    /* refuse the mismatched CBS library */
}
```

Supply explicit fetch, finalization, prune, command-path, and library-path
policy. Register `CbsBuildEventSink` for live progress and optionally consume
those events with `CbsBuildReport`. Callback data is borrowed for the
duration of each callback and event rejection fails the build closed.

For composed tool roots, pass `CbsToolIdentity` values through the same build
and fingerprint context. Each identity must cover every reachable file in the
declared command/library roots; files not covered by the supplied identities
are not represented by that identity-based key.

## Ownership boundary

cixd owns dependency solving, image construction, sandbox enforcement,
repository credentials, signing, publication, and rollback. CBS owns CPDL
validation, phase execution, source verification, manifest generation,
CIXPKG creation, artifact verification, and extraction. An embedder may
strengthen policy but must not reinterpret recipes as shell input or bypass
CBS's source and artifact verification.
