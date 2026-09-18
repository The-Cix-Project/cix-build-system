# ADR-0036: Staging shared libraries from the build sandbox

## Status

Accepted

## Decision

`stage library "NAME" into DIR` ships one shared library from the build
sandbox into the staged tree. CBS owns the search: `/usr/lib/TRIPLET`,
`/lib/TRIPLET`, `/usr/lib`, `/lib`, `/usr/lib64`, `/lib64`, first match
wins. The name must be a bare file name, checked by the validator and again
at runtime after interpolation. The copy preserves mode and symlink-ness.

This is a deliberate exception to path confinement, and it is framed as
reading declared dependencies rather than reading the host. CBS runs inside
the build container that cixd creates (ADR-0016), whose library directories
hold exactly the declared build dependencies' contents (ADR-0011). The
candidate list is the build image's layout; no recipe value ever names a
directory outside the confined roots. A staged library's bytes are those of
the pinned build image, so byte-identical rebuilds hold exactly when the
image is pinned — the same property the shell recipes' `cp -a` had
implicitly.

`/usr/lib/TRIPLET` is searched first because that is where merged-`/usr`
multiarch layouts place libraries; the shell recipes checked `/usr/lib`
first, and the two orders agree on any image that holds one copy.

## Rejected

`copy first_of { ... }`: it would put host directory names into recipes,
which is the list the ticket said would rot, and it would make confinement a
per-recipe decision. Naming the declared dependency that should have
provided a missing library: CBS has no library-to-package database, so the
diagnostic names the library and the directories searched instead.
