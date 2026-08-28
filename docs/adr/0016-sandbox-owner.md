# ADR-0016: Sandbox ownership

cixd creates and tears down the OS sandbox. CBS validates logical confinement
and staged-tree policy inside the supplied roots; it never creates a second
sandbox implementation.
