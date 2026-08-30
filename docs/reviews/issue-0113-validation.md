# Issue #113 validation: explicit target architecture

CBS now requires the build command to name its target architecture:

```text
cbs build recipe.cbs --arch x86_64 --staged ./stage --output ./pkg.cixpkg
```

The value is passed unchanged through the standalone pipeline, where the
existing canonical identity policy validates it and derives the package
identity (`name-version-release-arch`). The identity is also written into the
CIXPKG header, so artifacts for different targets cannot be mistaken for one
another by inspection or verification tooling.

The command no longer silently assumes `x86_64`; omitting `--arch` is a usage
error. TCC `-Wall -Wextra -Werror -pedantic`, the complete regression suite,
and a cross-target CLI smoke test pass.
