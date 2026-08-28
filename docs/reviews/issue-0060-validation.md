# Issue #60 validation

`cbs_observe_dependencies` is the single observation hook for cixd's ELF
dependency scanner. CBS propagates observer failures; platform-specific ELF
parsing is not duplicated in the build engine.
