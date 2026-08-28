# Issue #20 validation: command contracts

ADR-0008 defines all six commands, exit statuses, output ownership, and the
artifact-only `inspect`/`verify` contract. The existing `inspect` command is
non-executing; remaining adapters follow the cixd integration boundary.
