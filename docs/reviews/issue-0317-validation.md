# Issue #317 validation: make the package-level item set a checked contract like an output's

Follows #315. That ticket gated an output's items; the package's own items have
the same shape, and the owner asked for the same check.

## What it is

`tests/package-items-test.sh`. A row per item the parser accepts at package
level, taken from `parse_package_item`, stating what the validator, `explain`
and the manifest do with it. Each row declares the item in a recipe, validates,
explains, builds and lists it, and tries an invalid value. A cell that says "no"
must give its reason. It adds no capability and changes no behaviour.

Package items are declared in a fixed order, so a row carries a slot
(`version`, `release`, `format`, `ident`, `upstream`, `sources`, `tail`) and the
harness places the declaration where the order allows. One source and one cache
entry are shared so every row can be built, including `tools`, which builds
against `tcc`, always present on a host that runs the gate.

## What the table recorded

```text
              validator   explain   manifest
version          yes        yes       yes (identity)
release          yes        yes       yes (identity)
format           yes        yes       no  (the artifact is CIXPKG v2 by construction)
license, abi     yes        yes       yes
sources          yes        yes       yes
tools            yes        yes       yes (tool_policy)
upstream         yes        yes       no  (release discovery, not what was built)
requires         yes        yes       no  (explain-only at every level)
replaces         yes        yes       no  (explain-only at every level)
resources        yes        yes       no  (orchestrator metadata)
build_image      yes        yes       no  (orchestrator metadata)
capability       yes        yes       no  (orchestrator metadata)
toolchain        yes        yes       no  (the manifest records the requires compiler)
metadata         yes        yes       no  (author annotation)
privileged       yes        NO        yes (the staged entry's mode)
```

No cell was a defect, which is why this is a gate and not a fix. Two were
worth writing down as decisions: `toolchain` is recorded by the manifest as the
compiler named in `requires`, not the declaration, and a `privileged` allowance
is not in `explain` at all, its effect being the mode the typed manifest
retains. `architecture` is not a row: it is a validation error and is checked
directly. `output` is the other table.

## Evidence

The test passes on this engine and fails, each time on the cause it names, when
a row is removed, an explain pattern or a manifest pattern is wrong, a reason is
removed from a "no" cell, a row names an item the parser no longer accepts, and
`any` is no longer excluded.

## Found on the way

Building the `privileged` row showed that a `privileged file` declaration is
accepted when it matches nothing, although the specification says the path must
resolve to one file and the staged entry must match exactly. That is #318. It
is not fixed here, because refusing it would turn builds that succeed today into
failures and the corpus declares such allowances.
