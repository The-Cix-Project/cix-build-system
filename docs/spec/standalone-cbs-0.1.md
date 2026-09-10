# Standalone CBS language scope 0.1

CBS is a standalone, declarative build language and package engine. A `.cbs`
definition is parsed and validated before any phase executes. Its observable
inputs are the definition bytes, verified source bytes, declared dependency
set, target architecture, workspace policy, and CBS version.

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
verified CIXPKG and provenance record; no host daemon is required.
