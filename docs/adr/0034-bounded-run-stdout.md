# ADR-0034: Bounded CPDL command stdout

CPDL commands may expose a deliberately bounded stdout result for assertions
and later recipe values. `expect { stdout contains "text" }` asserts a literal
substring. `stdout "NAME"` binds the command result as `${stdout.NAME}`.

CBS captures at most 64 KiB, trims trailing whitespace, and accepts only one
line. Bindings are named, immutable values for the current execution context;
they are not shell fragments, are never re-parsed, and cannot create loops,
pipelines, or arbitrary variables. This supports bounded probes such as
compiler version and package metadata without turning CPDL into a scripting
language.

Recipes that need complete or multi-line output may use `stdout file PATH`.
CBS writes the captured bytes to the confined path after the command exits;
the file can then be checked with the existing `require file` assertions.
