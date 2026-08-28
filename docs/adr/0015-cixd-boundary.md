# ADR-0015: CBS and cixd boundary

CBS is a library/API invoked by cixd; cixd owns REST, authentication, image
transactions, and lifecycle. The CLI remains a thin diagnostic adapter.
