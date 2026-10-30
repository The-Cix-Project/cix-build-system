# Issue #303 validation: bind what the finalizer is, not that one ran

Opened from a follow-up noted while closing #300 and #302, and settled with the
cix maintainer and their advisor before implementing, at the owner's request.

## What was wrong

`run_build_pipeline` set `fingerprint_context.finalize_path` to the literal
`"callback"`, so **two different `--finalize-command` scripts produced the same
key**. A composer that changed its finalizer got a cache hit on an artifact the
previous one built. Every other composer input is bound by content: each
executable the recipe runs, `--tool-identity`, a firmware root, declared
inputs.

The marker existed because `main.c` holds the resolved path in
`finalize_policy.resolved` but it reaches `package.c` only as an opaque
`void *user` behind a function pointer, which `package.c` cannot interpret —
a deliberate boundary. So CBS had nothing to digest.

## Decisions, and whose they were

**Digest, not path.** The deciding fact came from cix, measured in cixd's
source: cixd passes `--finalize-command /build/finalize.sh` on every build, the
path never changes, and the content changes release to release. A path-bound
key would therefore give every finalizer cixd has ever shipped the same
identity — this ticket's defect one level up. `main.c` now digests the file it
resolved and passes the digest, so the key moves with content and not with
where the finalizer sits. Binding the path string as well was rejected: a key
that changes because the finalizer moved is not reproducible across hosts.

**One slot, not a new identity type.** The material loop already handles
`finalize_path` two ways as of #302: an absolute path is digested, anything
else is bound verbatim. So the whole mechanism existed and the new entry points
only thread a `const char *` into it. A composer passing an absolute path gets
content binding; one passing `"cixd-finalizer/2"` gets a declared identity.
One mechanism, no parallel.

**Fail closed for an unidentified callback.** This reversed the implementer's
lean. The objection was that refusing a key would make every cixd build keyless
again, having just fixed the opposite in #300 — and cix showed the objection
was built on a false premise: a process-level finalizer is *always*
identifiable, because the CLI digests the command it resolved, so a
`--finalize-command` caller changes nothing. The only finalizer CBS cannot see
is an in-process callback that declares nothing, which is an embedder's
omission. Binding a marker there would leave a material input out of a key that
looks complete — the shape ADR-0038 §6 refused for the report. The artifact now
records `build_fingerprint unavailable` with the reason
`finalizer-unidentified`.

**Not in `explain --json`.** `explain` reports what the recipe declares; a
finalizer is composer policy. The same line was drawn for `--output-dir`.

## Parity, which is the half that could have been missed

#300 established that `build` and `cbs fingerprint` agree for the same inputs,
and the docs named `--finalize-command` as one of three options the verb cannot
take. Making the build bind the finalizer *more* precisely while leaving the
verb unable to be told about it would have deepened that gap rather than closed
it. So `cbs fingerprint` now accepts `--finalize-command`, resolves it against
the approved command path and digests it exactly as the build does.

That is what makes the key usable for its stated purpose: a cache lookup
*before* building. `--prune-policy` and `--firmware-root` remain missing from
the verb and are filed separately (#305) rather than folded in.

## Acceptance criteria

`tests/finalizer-identity-test.sh`, new and in the gate, asserts on two
finalizers **at the same path** differing only in content — the shape cixd
actually has:

1. both keys are 64-hex and they differ;
2. the artifact records `finalizer` and it equals `sha256sum` of that file;
3. the verb's key equals the artifact's, in both the space and `=` forms, so
   the option is not being swallowed by the option parser's bare `else`
   catch-all;
4. an unreachable finalizer is a usage refusal in the verb, `CBS-E1006` — the
   code that already means this, not a new one;
5. a build with no finalizer records no `finalizer` line and still gets a key.

`tests/policy-test.c` asserts the fail-closed path: its embedder callback
declares nothing, so the artifact says `build_fingerprint unavailable` and
`finalizer-unidentified`. Read through the listing API, because the manifest
lives inside the container and not in its bytes — the first attempt scanned the
artifact's raw bytes and passed nothing.

Full gate green; all 1969 corpus recipes still validate.

## Not changed

`CBS_ABI_VERSION` stays 6: no struct layout changed. The two new entry points
are additions, so an existing embedder keeps its signature — and keeps its
behaviour, except that a callback it never identified now refuses a key instead
of binding a marker. That is the intended change and it is recorded here rather
than left to be discovered.
