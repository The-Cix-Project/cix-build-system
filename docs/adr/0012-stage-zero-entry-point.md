# ADR-0012: Stage-zero build entry point

- Status: Accepted
- Date: 2026-08-28

The repository `Makefile` is the stage-zero entry point. It accepts no ambient
compiler or library selection: `CC`, warning flags, source list, and approved
link libraries are fixed in the file. Stage zero therefore builds CBS directly
from the checked-out C sources using TCC and the ADR-0011 seed.

The interface is `make clean && make`; a missing or altered seed dependency is
a build failure, never a fallback. Once CBS exists, later stages may be
described in CPDL, but stage zero is intentionally not a CPDL recipe.
