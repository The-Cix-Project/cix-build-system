# Issue #292 validation: `check` and `validate` documented as aliases in opposite directions, with `--json` hidden on `check`

## Cause

The two verbs are implemented as exact synonyms, and nothing said so. `--help`
presented `check` as primary and `validate` as "Alias for check", while
`README.md` said `check` was "the intuitive alias for `validate`" — the
opposite direction. `--help` also showed `[--json]` on `validate` only.

Measured on v0.1.112 before the change: `check`, `validate`, `check --json` and
`validate --json` all print the same confirmation line, and each reports its
own verb in the `cbs.diagnostic/v2` envelope. Neither is subordinate to the
other anywhere, including in diagnostics.

## Decision

State the relationship once and identically: equivalent spellings, both taking
`--json`.

```text
cbs check RECIPE.cbs [--json]        Validate without executing
cbs validate RECIPE.cbs [--json]     Same as check
```

"Same as check" rather than "Alias for check" because an alias implies a
primary, and there is none. `--json` now appears on both lines, which is what
the binary accepts.

The user manual replaces "Use either `check` or `validate`" with an explicit
statement that they are one command under two spellings, that both take
`--json`, and that each reports its own verb — the last being the only
observable difference, and useful to a script.

`README.md`'s contradicting sentence is gone with the restructure in #293; its
command table now states the equivalence directly.

"intuitive" was dropped. Nothing else in these documents argues for a spelling.

## Acceptance criteria

1. `--help` advertises `--json` on both verbs and claims no primary.
2. `tests/cli-contract-test.sh`, which asserts help output and both verbs'
   behaviour, passes unchanged.
3. No document claims either verb is the other's alias —
   checked across `README.md`, `docs/` and `src/`.

## Scope

The help string is in `src/main.c`, so this is an implementation change and a
release. No behaviour changed: the verbs were already synonyms and still are.
