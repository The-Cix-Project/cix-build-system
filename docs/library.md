# CBS library boundary

`libcbs.a` contains the standalone CBS engine used by the `cbs` executable.
The installed public header is `cbs/cbs.h`, copied from `src/cbs_public.h`.
It exposes the standalone build, event/report, source-fetch, manifest, CIXPKG,
dependency-observation, and signature-verification APIs. Recipe lexer/parser
tokens, AST node layouts, and the internal execution structures in
`src/cbs.h` are private and are not installed as library ABI.

The current artifact is a static archive only: the build produces `libcbs.a`,
not `libcbs.so`, and there is no plugin-loader protocol in this repository.
The supported cixd first slice is therefore a parent process invoking the
`cbs` executable inside its container. Direct embedding is available to C
callers that deliberately link the public header and archive; they must treat
the published header and function signatures as the versioned boundary and
rebuild against the matching CBS release.

CBS does not create a sandbox, daemon connection, service lifecycle, image
transaction, or repository transaction. Those responsibilities belong to the
embedding/orchestration layer. Use `CbsBuildEventSink` for live progress and
`CbsBuildReport` for a persisted machine-readable summary; use the explicit
fetch, finalization, prune, observation, and signature callbacks where the
integration owns those policies.

Build and install with `make` and `make install PREFIX=/usr/local`.

The install also provides `lib/pkgconfig/cbs.pc`. A consumer can use the
matching static archive and header without repository paths:

```sh
cc $(pkg-config --cflags --libs --static cbs) consumer.c -o consumer
cbs --capabilities
```

The public header exposes `CBS_API_VERSION`, `CBS_ABI_VERSION`,
`CBS_CPDL_VERSION`, and `CBS_CIXPKG_VERSION`; applications can compare those
macros with `cbs_api_version()`, `cbs_abi_version()`, and
`cbs_execution_context_size()` before embedding. Upgrade the header, archive,
and executable together from the same CBS tag, then rerun the install
contract test. The supported cixd integration remains the child-process
contract; the static archive is an explicit embedding option.
