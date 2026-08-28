# ADR-0009: Third-party source policy

- Status: Accepted
- Date: 2026-08-28

CBS permits maintained third-party source only when a linked library is not
available on the supported base system. Vendored code lives under `vendor/`,
is pinned to an upstream release, carries its license and checksum, and is
compiled by the same TCC policy. Runtime/link dependencies remain governed by
ADR-0002; vendoring is not a waiver for unreviewed code.

Updates require a reviewed commit, refreshed checksum/license record, and
regression plus bounds tests. Network clients and package policy remain cixd
responsibilities.
