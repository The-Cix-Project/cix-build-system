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

`CBS_ABI_VERSION` is 6, and has been since v0.1.119. It was raised from 3 to 4
when `CbsExecutionContext` gained the build-wide event counter that makes event
sequence numbers monotonic, from 4 to 5 when `CbsBuildEvent` gained
`exit_status` and `signal` on `command-end`, and from 5 to 6 when
`CbsBuildReport` gained `output_count`. A consumer that links `libcbs.a` and was
built against an earlier ABI must be rebuilt; the guard below is what catches
it.

The number governs only consumers that link the static archive against
`cbs/cbs.h`, where struct layouts are compiled into the consumer. A consumer
that runs the `cbs` binary as a child process, which is how cixd integrates,
links no header and is unaffected by a bump: it needs the new `cbs` package and
nothing else.

`cbs_build_standalone_with_finalizer()` and
`cbs_build_standalone_outputs_with_finalizer()` are the current
full-capability entry points. Each is the one before it plus the
finalizer identity. An absolute path is digested; any other string is bound
verbatim, so an in-process callback can declare itself. A callback with no
identity refuses the key rather than binding a marker, and the artifact says
`finalizer-unidentified`.

The public header exposes `CBS_API_VERSION`, `CBS_ABI_VERSION`,
`CBS_CPDL_VERSION`, `CBS_CPDL_CONTRACT`, and `CBS_CIXPKG_VERSION`. `CBS_CPDL_CONTRACT`
is the language version (`"1.0"`), while `CBS_CPDL_VERSION` is its numeric
equivalent. Applications
can compare the API/ABI macros with `cbs_api_version()`, `cbs_abi_version()`,
and `cbs_execution_context_size()` before embedding. Upgrade the header,
archive, and executable together from the same CBS tag, then rerun the install
contract test. The supported cixd integration remains the child-process
contract; the static archive is an explicit embedding option.

## Consumer checklist

1. Obtain `cbs/cbs.h`, `libcbs.a`, and the `cbs --version` binary from one
   release tag.
2. Check `cbs_api_version()`, `cbs_abi_version()`, and
   `cbs_execution_context_size()` before constructing an execution context.
3. Provide explicit command and library roots, fetch/finalization policy, and
   event/report sinks; do not rely on the host environment.
4. If supplying `CbsToolIdentity` values, cover every reachable file in the
   composed command/library roots and pass the identical set used to compute a
   pre-build fingerprint.
5. Treat event callback pointers as borrowed for the callback duration and
   reject a build if the event sink rejects an event.

The process boundary remains the simplest integration: invoke `cbs`, consume
the JSONL event stream and `--report`, and retain the verified artifact. A
parent that also wants an operator-readable log passes `--events human` with
`--events-fd N` or `--events-file FILE`, which keeps the two streams apart
without an in-process sink. Use the static API when the caller needs
synchronous in-process policy control.
