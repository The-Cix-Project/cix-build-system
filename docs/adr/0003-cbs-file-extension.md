# ADR-0003: Use `.cbs` for package definition files

- Status: Accepted
- Date: 2026-08-28
- Owners: Cix project
- Scope: Package definition filenames
- Supersedes: The `.cpdl` filename examples in ADR-0001

## Decision

CBS package definition files use the `.cbs` extension. CPDL remains the formal
name of the language parsed by the `cbs` executable.

Examples:

```text
zlib.cbs
gcc.cbs
cbs.cbs
```

`.cpdl` is not a second accepted extension. CBS must reject or ignore it
according to the command being performed rather than supporting parallel
filename conventions.

## Context

ADR-0001 used `.cpdl` while the language and engine names were still being
settled. The preferred working vocabulary is “CBS file” or “CBS recipe,” while
CPDL remains useful when discussing the grammar and language semantics.

The user-facing filename should optimize the ordinary package-author workflow.
Users invoke `cbs`, edit a `.cbs` file, and produce a `.cixpkg` artifact. The
formal CPDL name does not require the filename suffix to repeat that acronym.

## Consequences

- Documentation and diagnostics call the language CPDL.
- Repository discovery and examples use only `*.cbs`.
- The canonical self-hosting definition is `cbs.cbs`.
- The parser does not need extension aliases or compatibility branches.
- Existing ADR-0001 `.cpdl` examples are historical context and are superseded
  by this decision.

## Rejected alternatives

### Use `.cpdl`

Rejected because it is less natural in the primary CBS workflow and was not yet
backed by shipped files requiring compatibility.

### Accept both `.cbs` and `.cpdl`

Rejected because two equivalent extensions create parallel conventions,
ambiguous repository discovery, and unnecessary compatibility logic before the
first implementation exists.

