# ADR-0030: Release default and identity normalization

An omitted package release defaults to `1`. CBS computes name-version-release
once and reuses it for filenames, metadata, and repository identity.

## Superseded in part (2026-09-19)

The default was never implemented. `src/validate.c` requires exactly one
`release` declaration, and CPDL 0.1 §3.1 lists release among the required
package declarations; omitting it is `CPDL-E3001`. A default would make two
recipes with different text produce one identity, which the second sentence
of this decision exists to prevent, so the requirement stands and this
paragraph records that the first sentence does not describe CBS.

Identity normalization is unchanged and remains in force.
