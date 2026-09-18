# CPDL and CBS test coverage

The default make test gate uses small deterministic tests. New CPDL or CLI
features must add focused success and failure assertions to this matrix.

| Surface | Test coverage |
| --- | --- |
| Lexing, parsing, validation, locations, JSON diagnostics | parser-validation.sh and invalid fixtures |
| Run argv, environment, jobs, expected exit, timeout, and limits | exec-test.c and argv.cbs |
| on_fail, allow_failure, continuation, and primary errors | runtime-test.c and failure.cbs |
| env, cd, mkdir, copy, move, remove, symlink, write, chmod, and globs | fs-test.c and filesystem.cbs |
| extract, materialize, and configuration assertions | extract-test.c and extract.cbs |
| replace, insert, and cardinality assertions | edit-assert-test.c and edit-assert.cbs |
| Sources, mirrors, checksums, cache, and fetch failures | source-test.c and fetch-test.c |
| CIXPKG, corruption, modes, links, round trip, and reproducibility | package, typed-package, fuzz, and repro tests; upstream-smoke-test.sh rebuilds zstd and compares bytes |
| validate, check, explain, inspect, build, verify, extract, help, and version | cli-contract-test.sh and cli-build-test.sh |
| Embedding seams, dependency observation, and policy callbacks | seams-test.c, observe-test.c, and policy-test.c |
| Every in-repository migrated recipe parses and validates | make recipe-test |

The CLI contract test asserts output and representative failure exit statuses.
Focused C tests assert operation results and diagnostic codes. The zstd package
qualification builds zstd twice and requires byte-identical artifacts; it is
included in make test and can use a caller-supplied CBS_UPSTREAM_CACHE
directory. The retired migration drafts are documented in issue #144 rather
than included as shipped fixtures.
