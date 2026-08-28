# ADR-0025: User namespace ownership

cixd creates userns-by-default containers and maps staged ownership through
the container runtime. CBS records manifest ownership and rejects unsafe
ownership requests but never configures host user namespaces.
