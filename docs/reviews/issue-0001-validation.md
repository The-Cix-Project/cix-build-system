# Issue #1 validation: CPDL 0.1 grammar and diagnostics

- Issue: `#1 Specify the CPDL v0.1 grammar and diagnostic contract`
- Date: 2026-08-28
- Result: Pass
- Deliverable: [`docs/spec/cpdl-0.1.md`](../spec/cpdl-0.1.md)

## Inputs reviewed

The specification was derived from and checked against:

- ADR-0001, which establishes the CBS/CPDL responsibility boundary, phases,
  dependency roles, TCC invariant, filesystem vocabulary, and no-shell rule;
- ADR-0002, which permits governed linked libraries while retaining one CBS
  orchestration entry point;
- ADR-0003, which selects `.cbs` as the sole recipe extension;
- Gitea issue #1 and its acceptance criteria;
- Gitea issues #2 and #3, which consume the grammar and diagnostic contract;
  and
- the supplied GCC shell recipe, used as the hostile real-world stress case.

## Acceptance traceability

| Issue #1 requirement | Specification section | Result |
| --- | --- | --- |
| Formal v0.1 grammar | Sections 2–4 | Covered |
| `package` and identity | Section 3.1 | Covered |
| Named sources | Section 3.2 | Covered |
| Dependency roles and kinds | Section 3.3 | Covered |
| Five ordered phases | Section 4.1 | Covered |
| `run` without implicit shell | Section 4.2 | Covered |
| Filesystem vocabulary | Sections 4.3–4.5 | Covered |
| Source-edit operations | Section 4.6 | Covered |
| Assertions and cardinality | Section 4.7 | Covered |
| `on_fail` and `allow_failure` | Sections 4.1, 4.2, and 7 | Covered |
| Separate non-executing validation | Section 5 | Covered |
| Located diagnostic contract | Section 8 | Covered |
| Stable exit-status classes | Section 9 | Covered |
| Explicit v0.1 exclusions | Section 10 | Covered |

## Decisions made during specification

### Canonical order is enforced

Top-level declarations, dependency groups, and phases have one canonical order.
This reduces parser recovery ambiguity, makes definitions easy to compare, and
prevents style variants from becoming parallel conventions.

### Interpolation is explicit and non-shell-like

Only `${name}` interpolates inside quoted strings. A bare `$`, `$name` inside a
string, spaces, quotes, semicolons, and glob characters remain literal bytes.
Bare typed CBS values such as `$jobs` remain available where the grammar expects
a value. Interpolation never creates more than one argument.

### `jobs` is metadata, not a guessed command option

`jobs $jobs` records the permitted concurrency for CBS accounting. It does not
guess whether a particular upstream tool expects `-j`, `--jobs`, an environment
variable, or another convention. The recipe supplies the tool-specific argument
explicitly, for example `"-j${jobs}"`.

### Shell escape remains absent

CBS passes an explicit argument vector to `execve`. CPDL cannot name common
command interpreters or `env` as the executable. Verified upstream scripts such
as `./configure` remain runnable because their program text comes from the named,
hashed source rather than being embedded as recipe shell syntax.

### Mutations are checked before they occur

Source replacements and insertions require an exact cardinality. The complete
match count is checked before atomic replacement, preventing half-applied edits
when upstream source changes.

### Failure cannot become success through diagnostics

`on_fail` retains the original failure. `allow_failure` is confined to diagnostic
commands inside `on_fail`, so it cannot weaken an ordinary build phase.

## GCC stress-case coverage

The grammar can structurally represent the key behavior observed in the GCC
recipe:

- one main source and named GMP, MPFR, and MPC extra sources with paired hashes;
- explicit build, runtime, test, and TCC bootstrap dependency roles;
- extraction and renaming of verified extra sources;
- literal, cardinality-checked source edits;
- out-of-tree configuration and builds through lexical `cd` scope;
- exact argument vectors, command-local environment, timeouts, and jobs policy;
- generated self-test files using block strings;
- deterministic CBS globs with exact assertions;
- staged installation, pruning, copying, modes, and symlinks; and
- failure diagnostics that cannot consume the original build failure.

The existing GCC wrapper-watcher workaround is deliberately not generalized.
Issue #10 must govern native helpers before CPDL admits such a capability. This
is an explicit exclusion, not an accidental grammar gap.

## Consistency checks

- The grammar contains no shell, pipeline, redirection, command-substitution,
  import, function, loop, or conditional production.
- Package creation and network activity are absent from phase operations.
- The only external compiler dependency accepted is TCC.
- `.cbs` is the only recipe extension.
- Parsing and validation are explicitly non-executing.
- Runtime errors preserve child status or signal details while CBS returns a
  stable category exit status.
- New syntax requires a specification change before implementation.

## Follow-up boundary

Issue #1 defines behavior; it does not implement a parser. Issue #2 is the next
work item and must translate this grammar into a lexer, AST, parser, validator,
and exhaustive positive/negative fixtures without adding syntax.

