# ADR-0011: Stage-0 trusted library admissions

- Status: Accepted
- Date: 2026-08-28

The stage-0 seed admits only the platform C library, `libarchive` 3.6.x, and
`libzstd` 1.5.x. The C library owns file/process primitives; libarchive owns
format parsing and confined entry enumeration; libzstd owns fixed-parameter
compression. Each is dynamically linked, license-reviewed (permissive
BSD-style or equivalent), reproducibly version-pinned by the base image, and
bootstraps from the existing TCC-built seed without a CBS dependency cycle.

Failures remain typed CBS errors (`CPDL-E6001` for archive failures and package
verification errors for compression). No TLS, HTTP, compiler, shell, or other
library is admitted: transport remains cixd-owned, and TCC remains the sole
compiler. Adding any library requires a new focused ADR answering the same
seven admission rules.
