# Standalone CBS scope and determinism contract

- Status: **non-normative.** The normative language definition is
  [the CPDL 1.0 specification](spec/cpdl-1.0.md); the artifact format is
  [CIXPKG v2](spec/cixpkg-1.0.md). This document states the scope and the
  determinism contract in one page, for a reader deciding whether CBS fits
  before reading 1300 lines of grammar. Where it and a specification disagree,
  the specification governs.

CBS is a standalone, declarative build language and package engine. A `.cbs`
definition is parsed and validated before any phase executes. A build's
observable inputs are a closed list: the definition bytes (which pin each source
by SHA-256 and declare the dependency set), the verified source bytes, the
target architecture, the CBS version, and what the composer supplies and CBS
records — the approved command and library paths, the content of every
executable the recipe runs, any named inputs, a firmware root, a prune policy, a
finalizer, and toolchain identities. Nothing else reaches a build: not the
ambient environment, not an undeclared host tool, not the clock, not the
network during a phase, and not the caller's inherited descriptors or standard
input. Host executables do run, resolved only through the approved command
path, and the build records their content rather than assuming it.

`cbs fingerprint` computes that list as one key without running anything, so
the contract is stated in a form a machine can check. That closed list is what
makes a rebuild comparable, and `make test` also asserts it by building the
same recipe twice and requiring byte-identical artifacts.

Phases execute in the fixed order `prepare`, `configure`, `build`, `check`,
and `install`. Filesystem effects are confined to CBS-owned `src`, `build`,
and `dest` roots. `run` executes a declared executable directly; shell
interpretation, ambient compiler discovery, undeclared dependencies, and
network access from phases are forbidden. Source transport, caching, package
creation, inspection, verification, and HTTP/HTTPS fetching work without cixd.
cixd integration is an optional adapter for centralized transport policy,
external sandboxing, and image transactions.

Validation failures are deterministic, located, and non-executing. Runtime
failures preserve the primary cause and phase. A successful build emits a
verified CIXPKG — one per declared `output`, which is one unless the recipe
splits its staged tree — and provenance records; no host daemon is required.
