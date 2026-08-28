# Issue #2 validation: CPDL lexer, parser, AST, and validator

- Issue: `#2 Implement the CPDL lexer, parser, and validator`
- Date: 2026-08-28
- Result: Pass
- Implementation commits: `4d2d511` and the follow-on validation hardening
  commits through `e747937`

## Delivered boundary

CBS now provides a TCC-built, non-executing validation pipeline:

```text
UTF-8 .cbs source
        |
        v
      lexer
        |
        v
   located tokens
        |
        v
      parser
        |
        v
       AST
        |
        v
separate validator
        |
        v
 valid definition or located diagnostics
```

The implementation contains no executor. A valid `run` operation becomes AST
data only. A committed fixture names a nonexistent executable and validates
successfully, proving that validation does not execute phases.

## Source responsibilities

| File | Single responsibility |
| --- | --- |
| `src/cbs.h` | Shared token, AST, location, and API types |
| `src/ast.c` | Allocation, AST ownership, and token/AST destruction |
| `src/diag.c` | Stable located diagnostic rendering |
| `src/lexer.c` | UTF-8 validation and CPDL tokenization |
| `src/parser.c` | Grammar recognition and AST construction |
| `src/validate.c` | Non-executing semantic validation |
| `src/main.c` | File loading and the `cbs validate` entry point |

There is one lexer, one parser, one AST representation, and one validator. No
alternate or compatibility implementation exists.

## Lexer coverage

The lexer implements:

- UTF-8 validation with scalar-based diagnostic columns;
- BOM rejection;
- CRLF normalization and bare-CR rejection;
- comments and whitespace;
- ASCII identifiers and keywords;
- checked decimal integers, modes, and durations;
- quoted-string escapes with NUL and surrogate rejection;
- indentation-normalized block strings;
- CBS supplied-value tokens; and
- punctuation used by CPDL 0.1.

All numeric conversion is range-checked before values reach the AST. Leading
zeroes in decimal values are rejected rather than silently normalized.

## Parser coverage

The parser recognizes every CPDL 0.1 production:

- package identity and architecture;
- main and extra named sources;
- all dependency roles and kinds;
- all five phases;
- `run`, arguments, local environment, jobs, timeout, expected status, and
  diagnostic-only `allow_failure`;
- lexical environment and `cd` blocks;
- the complete filesystem vocabulary;
- named-source extraction;
- exact-cardinality replacement and insertion;
- file, directory, and glob assertions; and
- final `on_fail` blocks.

Malformed syntax stops before validation or execution and emits a located
`CPDL-E2xxx` diagnostic.

## Validator coverage

The separate validator checks:

- required, unique, and canonically ordered declarations;
- package, version, source, dependency, and environment spelling;
- exactly one main source and unique named sources;
- absolute URL shape and lowercase SHA-256 spelling;
- dependency role order and duplicate tuples;
- TCC as the only compiler dependency;
- phase order and uniqueness;
- CBS values and `${...}` interpolation references;
- prohibition of command interpreters as `run` executables;
- duplicate run options and command-local environment names;
- jobs, timeout, mode, and expected-exit bounds;
- `allow_failure` confinement to `on_fail`;
- declared-source references;
- nonempty source-edit match values;
- assertion kinds; and
- CPDL glob syntax.

Validation performs no source fetching, dependency resolution, filesystem
inspection, or process creation.

## Verification performed

The normal gate was run from a clean tree:

```text
make clean
make
make test
```

TCC compiled every translation unit with:

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

The same complete fixture suite was then run against a separately produced TCC
bounds-instrumented executable using `tcc -b`.

The fixture matrix contains positive complete/minimal/architecture examples and
negative lexical, parse, validation, ordering, duplication, range, reference,
TCC-policy, no-shell, extension, and diagnostic-location cases. It also covers
CRLF, BOM, invalid UTF-8, CLI misuse, and help output.

## Result

- Normal warning-clean TCC build: Pass
- Normal regression suite: Pass
- TCC bounds-instrumented regression suite: Pass
- Validation-without-execution fixture: Pass
- `.cpdl` extension rejection: Pass
- Located diagnostic shape: Pass
- Worktree artifact cleanup: Pass

Issue #3 may now implement process execution against this single AST and must
not add parser syntax or an alternate recipe-reading path.

## Backlog reconciliation

The implementation remains present on `main` and was revalidated while
reconciling the open tracker entry on 2026-08-28. The current `make test` gate
reports 49 parser/validation cases in addition to the execution and source
tests, with zero compiler warnings under TCC's mandatory warning policy.
