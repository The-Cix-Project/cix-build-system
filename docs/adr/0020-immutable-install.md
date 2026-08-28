# ADR-0020: CBS install and immutable images

CBS verifies and stages packages; cixd creates a new immutable image version,
retains the prior version, and owns garbage collection. CBS never mutates a
running or existing image root.
