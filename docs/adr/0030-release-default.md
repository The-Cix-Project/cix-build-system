# ADR-0030: Release default and identity normalization

An omitted package release defaults to `1`. CBS computes name-version-release
once and reuses it for filenames, metadata, and repository identity.
