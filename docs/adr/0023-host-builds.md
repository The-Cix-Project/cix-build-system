# ADR-0023: Host build future

Control-plane self-builds are deferred. CBS builds execute through cixd's
sandboxed worker boundary; direct host-root builds are unsupported until an
explicit security and lifecycle decision is accepted.
