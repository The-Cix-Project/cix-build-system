# ADR-0035: Parse-time list iteration

## Status

Accepted

## Decision

CPDL gains `each "NAME" in { "item" ... } { body }`. The body is expanded
once per item at parse time, by the same mechanism that already expands
`require … for { }` and `run … each`. `${each.NAME}` is a placeholder
replaced in quoted strings before validation; block strings are untouched and
the bare `$each.NAME` form is rejected, so the placeholder follows the
interpolation rules every other value follows. Items are quoted strings only:
a bare value reference as an item would not interpolate once pasted into a
string. Nested `each` bodies must bind distinct names.

Each expansion is its own block with scoped `env` bindings and its own
`on_fail`, and a failure names the bound name, item ordinal, and item after
the operation's own diagnostic.

## Rejected

A runtime loop with a bound variable, and conditionals. Both make the plan
depend on execution, so `explain` could no longer count operations before a
build, and both are the first step toward the scripting language ADR-0034
closed the door on. Iteration over a literal list with a compound body was
the one remaining shape the Cix recipe corpus needed; first-match selection
and pattern edits are decided separately.
