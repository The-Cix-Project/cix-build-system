# Issue #288 validation: CPDL 1.0 §2.4's keyword set lists 70 of 109 keywords, including the phase name `install`

## Decision

§2.4 now lists all 109 keywords, alphabetically, reading down each column, with
a sentence saying what the list covers: declaration names, phase and operation
names, operation options, and assertion property names.

The 39 that were missing:

```
alias args at before case checksums executable for forbids input
install key leaf license line_start links member memory metadata mode
needs nonempty origin parents patch privileged reason replaces
resources same_as signature signed-tag stderr stdout strip tag tools
truncate verify
```

This is not a cosmetic list. The sentence after it is normative — "Keywords
reserved for later versions are not silently accepted. An unknown word is an
identifier only in the few positions where this grammar explicitly permits
one" — so the list defines what is reserved. Read literally, `install`,
`license`, `metadata`, `replaces`, `resources`, `tools` and `privileged` looked
available for a recipe author's own use, when all seven are reserved and
matched by the parser.

No implementation change. The parser was right throughout.

## Derivation

Three sources, unioned, then the EBNF's character-class noise (single letters,
digits, the duration suffixes) removed:

1. quoted literals in every `ebnf` block of the spec;
2. `is_word(parser, …)`, `consume_word(parser, …)` and
   `strcmp(…->text, …)` in `src/parser.c`;
3. `require` kinds and assertion property names, which the parser keeps as a
   name token and `src/validate.c` checks — `directory` is reachable only
   this way.

Each addition was checked as a real keyword rather than a string value.

**Three traps for whoever re-audits this**, each of which silently shrinks a
naive grep:

- `sha256` is missed by `[a-z_]+`: it contains digits.
- `after` is missed because it is a ternary —
  `consume_word(parser, insert ? "after" : "from")`.
- `config`, `needs` and `forbids` are missed because they are compared against
  `->text` rather than passed to `is_word`.

All four words that §2.4 listed but the EBNF scan did not reach (`after`,
`config`, `directory`, `sha256`) are real, so nothing was removed.

## One documentation-shape finding

`for` is described only in §4.7 prose — "Any require operation may be followed
by `for { text-value … }`" — and appears in no EBNF production. It is in the
keyword list now, but the formal grammar still does not contain the construct.
Recorded rather than fixed: writing that production is a grammar change, not a
list correction.

## Acceptance criteria

1. The list contains 109 entries and every one is matched by the parser or
   checked by the validator.
2. Nothing was dropped: all 70 previous entries remain.
3. `make -j1 test` unchanged — this is documentation, and the 67 parser cases
   already cover the behaviour the list describes.

## No regression guard added

A test comparing §2.4 against the parser was considered and rejected. The
direction that broke is *completeness*, and extracting the parser's keywords
robustly in shell is what the three traps above make fragile; a guard that
produced false failures would be worse than none. The derivation above is the
durable artifact instead, so the next audit is cheap and correct.
