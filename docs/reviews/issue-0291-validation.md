# Issue #291 validation: `require tool … for` reports a generic "expected phase operation" instead of naming the rule

## Decision

The refusal stays a parse error. It now names the rule and uses the code the
diagnostic contract already allocates for it:

```text
require-tool-for.cbs:6:29: error[CPDL-E2002]: parse: `require tool` does not
  take a `for` list; its operand is a tool name resolved against the approved
  command path, not a target
```

`CPDL-E2002` — "Unexpected token or trailing input" — was allocated in §8.2 and
emitted nowhere. A `for` following `require tool` is exactly an unexpected
token, so this needed no new code and no change to the contract. `each` already
works this way: a construct-specific rule violation reports under its own code
(`CPDL-E2003`) rather than falling through to the generic
`expected <grammar element>`.

`parse_require()` returns as soon as it reads a `tool` kind, because that form
has no property block. It now checks for a trailing `for` before returning, so
the rule can report itself; the enclosing operation loop would otherwise meet
the token and say only that an operation was expected — true, but naming
neither the rule nor the reason.

`parse_unexpected()` is a sibling of the existing `parse_error()` rather than a
code parameter threaded through it: all six of that function's call sites are
`each` rules under `CPDL-E2003`, so two small functions each bound to one
documented code is clearer than one function with the code at every call.

## Correction to the report

The ticket claimed the old diagnostic pointed "one token early", at `require`
rather than at `for`. That was wrong. The caret was already under `for` at
column 29; only the message was at fault. The claim was inferred from the
control flow — the enclosing loop reports it — without checking the column.
Recorded because measuring was the whole point, and a correction posted on the
ticket.

## Acceptance criteria

1. **The rule names itself** — `tests/fixtures/invalid/require-tool-for.cbs`
   with a `.expect` pinning
   `error[CPDL-E2002]: parse: `require tool` does not take a `for` list`.
   `parser-validation.sh` discovers fixtures automatically and went from 67 to
   68 cases.
2. **The five kinds that take `for` are unaffected** — `file`, `directory`,
   `glob` and `config` with a `for` list, and a plain `require tool`, all still
   validate.
3. `CPDL-E2002` goes from unused to used; §8.2 already documents it, so no
   specification change.

## Not changed

The grammar. §4.7 already excludes `for` from `require-tool` after #290, which
is what makes a parse error the correct outcome here rather than a validation
error.
