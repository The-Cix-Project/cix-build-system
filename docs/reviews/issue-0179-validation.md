# Issue #179 validation: `env` rejects lowercase names, which blocks autoconf cache variables

## Decision

Environment names are POSIX portable names in either case:
`[A-Za-z_][A-Za-z0-9_]*`. The uppercase-only rule in
`valid_environment_name()` was a convention, not a platform constraint;
`execve` refuses only `=` and NUL. The portable character set is kept rather
than widened to everything `execve` allows, because a name with punctuation
cannot be read by `make` or a shell, which is where cache variables go next.
No `env raw` escape is needed.

## Acceptance criteria

1. `env "ac_cv_prog_CC" = "tcc"` validates and reaches the child —
   `tests/fixtures/execution/argv.cbs` sets it on a `run`, and the
   `exec-test` probe returns a distinct failure code unless
   `getenv("ac_cv_prog_CC")` is `tcc`; the `--probe-without-env` command
   asserts it did not leak to the next command. `tests/fixtures/valid/complete.cbs`
   sets a lowercase name at block scope and inside a `run`.
2. Names `execve` cannot carry are still refused — `tests/fixtures/invalid/environment.cbs`
   (`CC=tcc`) and `environment-punctuation.cbs` (`with-dash`) both expect
   `invalid environment name`. A NUL cannot appear in a CPDL string.
3. `recipes/package/htop` — converts with `env "ac_cv_prog_CC" = "tcc"` on
   its configure `run`; the recipe lives in the Cix tree and is not changed
   here.

## Docs

CPDL spec §4.2 `env` paragraph; user manual §11.4.
