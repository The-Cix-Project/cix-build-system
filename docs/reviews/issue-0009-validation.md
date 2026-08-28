# Issue #9 validation: dependency roles and kinds

- Issue: `#9 Implement dependency roles and kinds`
- Result: Pass
- Implementation: the commit containing this review

`src/dependency.c` converts the validated `requires` AST into a role-aware,
exact-name dependency set. It deliberately contains no version expressions or
solver: CPDL 0.1 names repository packages exactly.

Role effects are explicit. `bootstrap` and `build` inputs apply through all
construction and check phases; `test` inputs apply only to `check`; `runtime`
inputs apply to `install`. `cbs_dependency_set_contains` is the single
consumer lookup primitive. Invalid roles remain validation errors, and the
existing validator enforces unique ordered groups, unique tuples, package-name
syntax, and TCC-only compiler dependencies.

The integration test proves the complete fixture yields three build inputs
(build plus bootstrap), four check inputs (build, bootstrap, and test), and one
install input (runtime), with exact kind/name lookups and no test tool in build.

`make clean && make && make test` passes with warning-as-error TCC flags; the
full regression gate remains green. This ticket introduces no alternate
dependency representation or external resolver.
