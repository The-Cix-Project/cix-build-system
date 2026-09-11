# Issue #118 validation: embedding seams

CBS deliberately does not implement a second daemon, sandbox, image
transaction, or key-management system. The remaining adapter functions are
the documented embedding boundary: sandbox entry/leave, detached signature
verification, daemon requests, health checks, and ELF dependency observation.
The transaction placeholder was removed under issue #120.

`tests/seams-test.c` now exercises every remaining seam with supplied callbacks,
and the ELF observer is additionally tested against a real binary. This makes
the interfaces visible to the repository's test graph without pretending that
standalone CBS is the cixd implementation.
