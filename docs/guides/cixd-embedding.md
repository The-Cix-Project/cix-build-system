# cixd embedding guide

CBS has two supported integration shapes. Choose one deliberately for each
deployment. They have different compatibility rules, and mixing them up costs
effort for nothing: the **process boundary** is versioned by the `cbs` binary
and its documented options, exit statuses, events and artifacts; the
**in-process boundary** is additionally versioned by `CBS_ABI_VERSION`, because
it links `libcbs.a` against `cbs/cbs.h` and bakes struct layouts into the
consumer. A process-boundary consumer links no header, so an ABI bump does not
affect it — rebuilding the `cbs` package is all it needs.

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
    --events human \
    --events-fd 3 \
    --report REPORT.json
```

A recipe that declares `output` blocks emits several packages from one build,
and then `--output ARTIFACT` is refused, because one path cannot name several
artifacts. Pass `--output-dir DIR` instead; CBS writes one canonically named
`name-version-release-arch.cixpkg` per output, and either every artifact is
written or none is:

```sh
cbs build RECIPE.cbs --arch ARCH --staged WORKSPACE --output-dir ARTIFACTS \
    --cache CACHE --events-fd 3 --report REPORT.json
```

`explain --json` reports an `outputs` array — always, main package first, even
for a recipe with a single output — giving each output's name, file selectors
`requires` and `abi`, so you know what a recipe provides before building it. The
`package` declaration is itself the main output and receives every staged path
no `output` claimed. Because a selector naming a file does not claim the
directories above it, the main package is still emitted whenever directory
entries remain; it is omitted only when the outputs claim everything.

A package may also declare the ABI it offers its dependents (`abi "17"`), and
`explain --json` carries it on the top level and on each `outputs` entry, null
when undeclared and never inherited by an output. Read it from `explain`: it is
available before the provider's artifact is fetched, which is when a decision
about a provider's roll has to be made, and the installed artifact repeats it as
`metadata abi`. This is the ABI a package offers its dependents, and has nothing
to do with `CBS_ABI_VERSION` above.
`--output` remains the form for a recipe with one output. With several, the
report's `output_count` is greater than 1 and its `artifact` and
`artifact_digest` are empty rather than naming one of N; the
`artifact-finalized` events enumerate them.

Use `explain --json` before execution to select an image and compose the
declared dependencies. Forward JSONL events to the interactive UI and retain
the final report for build history. Use `--diagnostics=jsonl` on commands that
can fail; diagnostics are machine-readable on stderr and do not replace the
event stream. Every diagnostic carries a code whose prefix names its origin —
`CPDL-` for a located recipe diagnostic, `CIXPKG-` for an artifact failure,
`CBS-` for a failure of the command itself — and the user manual's section 8.1
lists every `CBS-` code with its exit status. A code keeps its meaning once
assigned, so matching on it is safe.

The two event options above are independent, and a forking parent wants both.
`--events` selects the reporter on standard error; `--events-fd N` (or
`--events-file FILE`) writes the JSONL stream to a channel the parent owns. So
`--events human --events-fd 3` leaves the operator's log and the build
commands' own output on standard error while the parent reads structured events
on descriptor 3, with no per-line disambiguation. Without a destination,
`--events jsonl` puts machine records on the same descriptor as every child's
output, which is why selecting `human` for the operator used to mean giving up
the structured stream.

A build command cannot reach any of this. Its environment is constructed, its
standard input is `/dev/null`, and every descriptor above standard error is
closed before `execve`, so the event channel, the events file and anything the
parent left open are not inherited. Passing an event descriptor therefore does
not make it writable by the build it reports on. The descriptor is validated
against the parent's table before CBS opens anything of its own.

CBS duplicates the descriptor, so closing its reporter never closes one the
parent still owns. A descriptor that is closed or not open for writing is a
usage refusal (exit 2, `CBS-E1022`) before the build begins, so a parent never
gets a successful build that silently reported nothing.

After a successful build, verify the artifact before publication:

```sh
cbs verify ARTIFACT.cixpkg
cbs list ARTIFACT.cixpkg --json
```

The artifact's `build_fingerprint` provenance is the value consumers should
store with the cache record. A precomputed `cbs fingerprint` key must be given
the same inputs as the build: `--arch`, `--command-path`, `--library-path`,
`--input` bindings, the complete `--tool-identity` set, and — since v0.1.123
and v0.1.124 — `--finalize-command`, `--prune-policy` and `--firmware-root`.
Every input that enters the key can be given to the verb, so there is no
option whose use makes a build unpredictable.

A finalizer is bound by content: CBS digests the command it resolved, so two
finalizer scripts at the same path produce different keys. An embedder that
supplies a finalize callback without declaring an identity gets no key at all
(`build_fingerprint unavailable`, reason `finalizer-unidentified`) rather than
a key that silently omits it.

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

The current language contract is CPDL `1.0`. In capability JSON,
`cpdl_contract` is the authoritative language version and `cpdl_version` is its
numeric equivalent.

Supply explicit fetch, finalization, prune, command-path, and library-path
policy. Use `cbs_build_standalone_with_finalizer()`, or
`cbs_build_standalone_outputs_with_finalizer()` for a recipe with several
outputs, and pass the finalizer's identity: an absolute path is digested, and
any other string is bound verbatim, which is how an in-process callback
declares itself. A callback with no identity yields no key, as above.

Register `CbsBuildEventSink` for live progress and optionally consume those
events with `CbsBuildReport`. Callback data is borrowed for the duration of each
callback and event rejection fails the build closed.

For composed tool roots, pass `CbsToolIdentity` values through the same build
and fingerprint context. They bind the toolchain by verified manifest digest
instead of resolving anything on the host. CBS does not hash the rest of the
command or library path, so an identity set that omits a file the build can
reach leaves that file out of the key; make the set as complete as the
toolchain you mean to bind.

## Ownership boundary

cixd owns dependency solving, image construction, sandbox enforcement,
repository credentials, signing, publication, and rollback. CBS owns CPDL
validation, phase execution, source verification, manifest generation,
CIXPKG creation, artifact verification, and extraction. An embedder may
strengthen policy but must not reinterpret recipes as shell input or bypass
CBS's source and artifact verification.
