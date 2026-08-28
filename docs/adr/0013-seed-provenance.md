# ADR-0013: Seed provenance metadata

Every CBS build records the TCC version, source revision, and admitted seed
library names, versions, ABIs, and licenses. Missing provenance is a build
failure; ambient libraries are never substituted.
