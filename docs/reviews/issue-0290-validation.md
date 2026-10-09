# Issue #290 validation: `require-operation` omits `for`, omits `require config`, and its prose claims `for` works on `require tool`

## Decision

§4.7 now defines what the implementation accepts:

```ebnf
require-operation = ( require-file | require-directory | require-symlink
                    | require-glob | require-config ), [ for-list ]
                  | require-tool ;

for-list = "for", "{", text-value, { text-value }, "}" ;
```

`require-config` is written out as a production and its description moved from
§4.5 — the Extraction section — into §4.7 with the other assertion kinds.
`config-state` is `"y" | "m" | "n" | "absent"`, verified by accepting all four
and rejecting a fifth.

`text-value, { text-value }` rather than `{ text-value }` because the
non-empty rule is already enforced: an empty `for { }` is rejected with
`CPDL-E3004: list declaration must contain at least one value`.

No implementation change. The parser and validator were right on all three
points.

## The framing that was wrong

`require tool … for { }` is rejected, while the prose said "Any require
operation may be followed by `for`". The first reading of that was "#286 in
reverse": spec more permissive than parser, so ruling the spec authoritative
would mean changing code to make `for` work on `require tool`.

That was a mistake, and it is worth recording because it would have turned a
documentation fix into a language change. §4.7's sentence is an informal gloss
about a grammar that **does not contain `for` at all**. There was no normative
production for it to outrank, so there were never two authorities to choose
between — the grammar was silent, which makes the question what it should say.

Answering that from the construct: `for` replaces the target an assertion
inspects. `require tool`'s operand is neither a path nor a pattern — it is a
tool name resolved by `cbs_resolve_executable()` against the approved command
search path, and `require tool` is the only form with no property block. The
early `return` in `parse_require()` is the parser correctly declining to
overload path substitution onto an operand of a different kind. So the grammar
excludes it, and the prose now says why.

Note the distinction is *not* "takes a path": `require glob` takes a pattern
string, not a `path-value`, and accepts `for`. The line is between a target in
the build filesystem and a name resolved outside it.

## Verified semantics

`for` clones the require once per value and replaces its target operand.
Expansion is at parse time, like `each`: a phase with two `write`s and
`for { a b }` reports 4 operations from `cbs explain`. Confirmed accepted on
`file`, `directory`, `glob` and `config`, and rejected on `tool`.

## Acceptance criteria

1. The production admits exactly the six kinds the validator accepts, with
   `for-list` on the five that take a property block.
2. `require config` has a production and its semantics are described in §4.7.
3. `make -j1 test` unchanged — documentation, and the behaviour is already
   covered by the 67 parser cases and the `for`/`each` expansion counts
   asserted in `cli-build-test.sh`.

## Deliberately excluded

The diagnostic for `require tool … for` is
`CPDL-E2001: parse: expected 'phase operation', found 'for'`, which reads as
though `for` were an unknown word rather than unsupported in that position.
Left alone: now that the grammar says `for` does not appear there, a parse
error is the correct outcome, and a friendlier message would be code added
under a documentation ticket.
