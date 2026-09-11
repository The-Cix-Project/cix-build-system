# Issue #125 validation: canonical CBS source

`cbs.cbs` now points to the immutable `v0.1.21` repository archive and carries
its verified SHA-256 digest. The previous all-`a` placeholder and moving
`main` source were not trustworthy recipe metadata: changing the recipe could
change the bytes being used to build CBS.

The recipe metadata test rejects placeholder checksums and accepts only an
exactly 64-character lowercase hexadecimal digest. The repository now also
contains the migrated seed recipes listed in `recipes/README.md`; this is
explicitly a seed set, while the external shell corpus remains a separate
migration workstream.
