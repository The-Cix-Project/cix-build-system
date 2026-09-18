# Issue #176 validation: No way to apply the same steps to each item of a list, and 15 packages need one

## Decision

ADR-0035 adds parse-time list iteration:

```cbs
each "lib" in { "libcap.so.2" "libcap.so.2.66" "libcrypto.so.3" } {
    copy "${build}/staging/${each.lib}" to "${dest}/usr/lib/${each.lib}"
    require file "${dest}/usr/lib/${each.lib}" { exists }
}
```

The body is expanded once per item by the same mechanism that already
expands `require … for { }` and `run … each`, so the plan stays static and
`explain` counts every expanded operation. `${each.NAME}` is a placeholder
replaced in every quoted string of the body before validation; block strings
are untouched and the bare `$each.NAME` form is rejected with a hint. Items
are quoted strings only. Nested `each` bodies must bind distinct names.

Each expansion is its own block: `env` bindings end with the item, an
`on_fail` at the end of the body runs for the failing item, and after the
operation's own diagnostic a `CPDL-N4002` note names the bound name, the
item ordinal, and the item. `CPDL-E2003` covers an empty item list, an empty
body, a non-identifier name, and a shadowed name.

## Acceptance criteria

1. A body of two or more operations per item — `tests/fixtures/execution/each.cbs`
   runs `write`, `copy`, and `require` per library.
2. The item substitutes anywhere a string value does — paths, written text,
   `copy` destinations, `require` targets, `env` values, and `run` arguments
   in the fixture; the spec states the rule.
3. A failure names the item — `tests/each-test.c` checks that the second
   item's `CPDL-E4005` is followed by ``note[CPDL-N4002]: … while processing
   each `item` item 2 (`missing`)`` and that the third item never runs.
4. `recipes/package/coreutils` in the Cix tree — the fixture mirrors that
   loop's shape (three libraries, several operations each). The copy-out
   step of that recipe also needs first-match selection across multiarch
   directories, which is #178, so the full conversion is not claimed here.

Also covered: nested `each`, `each` inside `cd`, per-item `env` scope (the
binding is visible to `run` inside the item and absent after the loop),
`explain --json` reporting 21 and 9 expanded operations (the body's `on_fail` counts as one) for the fixture's
phases (`tests/cli-build-test.sh`), and six invalid fixtures with `.expect`
text.

## Docs

CPDL spec §4.1 grammar, §4.8 Iteration, keyword list, §9 `CPDL-E2003`;
ADR-0035; user manual §6; `docs/cpdl-test-coverage.md`.
