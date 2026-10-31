# Issue #307 validation: upstream validation errors carry the CPDL- prefix

Found by the documentation sweep, specifically by making the diagnostic-code
registry checkable: thirteen validation errors in `src/validate.c` were emitted
as `CBS-E3002`, `CBS-E3004` and `CBS-E3006`.

## What a user saw

```text
recipe.cbs:2:3: error[CBS-E3004]: validation: upstream tag template must
  contain {version} and only known placeholders
```

`E3xxx` is the documented *validation* range, listed in the specification's
section 8.2 under `CPDL-`. The `CBS-` prefix belongs to the command's own
diagnostics (`CBS-E1xxx`). So the user got a validation code in a namespace no
document defined, and a consumer matching `CPDL-E3004` would have missed it.

## Cause

One commit, `ff700c7` (2026-10-02, "Add structured upstream discovery
metadata"), wrote the `upstream` validation with a different prefix: thirteen
sites, `CBS-E3002` once, `CBS-E3004` eight times, `CBS-E3006` four times, beside
more than a hundred correct `CPDL-E3xxx` sites in the same file. Nothing
asserted the spelling and no document listed the codes, so it lasted four weeks.

An existing check had a partial view of it: `parser-validation.sh` flags a
diagnostic that does not match the documented shape as *malformed*. It stops at
the first failure, and no fixture reached these sites, so it never ran.

## Decision

Normalise the prefix: `CBS-E3xxx` becomes `CPDL-E3xxx` at all thirteen sites.

**This is not a repurposing**, which the append-only rule forbids. Each stray
site already matched the documented meaning of the number it carried:

| Code | Documented meaning | Stray sites |
| --- | --- | --- |
| `E3002` | duplicate declaration or option | at most one tag, source and verification |
| `E3004` | invalid name or literal value | tag and source templates, verification key |
| `E3006` | operation is invalid in this context | unsupported provider or method, unknown parameter |

No test, fixture or document asserted the `CBS-E3` spelling, so no published
contract changed. The one plausible consumer, cixd, was asked: it matches
neither `CBS-E` nor `CPDL-E` codes anywhere, keeps `cbs`'s stderr and reports it
verbatim, so nothing is needed on its side.

## Acceptance criteria

Three invalid fixtures reach the sites from real recipes, with `.expect` files
requiring the exact emitted text: an upstream tag template and a source template
lacking `{version}` (`CPDL-E3004`), and a duplicated parameter (`CPDL-E3002`).
The parser suite goes from 71 to 74 cases, and the three fail against the
v0.1.125 engine.

The `E3006` sites are guarded structurally rather than by fixtures, because the
recipes that reach them are parse errors first. `tests/docs-contract-test.sh`
now fails if the source can emit any code that no document lists, so a stray
prefix cannot return quietly: it would be a code in no table.

Full gate green; all 2013 corpus recipes still validate.

## Recorded by the same search, with no code change

`CBS-E0000` is the emitter's fallback when a diagnostic is raised with no code;
`CBS-E4000` relays a runtime failure while diagnostics are machine-readable.
Both were emitted and undocumented. They are now rows in the manual's section
8.1 saying what they do, and `CPDL-N4001` is named in the specification where
its behaviour was already described.
