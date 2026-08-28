# Cix Build System

CBS is the command-line package build engine for Cix. It consumes package
definitions written in CPDL and stored as `.cbs` files.

The project is in its first implementation phase. The current executable can
lex, parse, and validate the complete CPDL 0.1 grammar without executing package
phases.

The production runtime also implements direct `execve` execution for validated
`run` AST nodes. This API is intentionally not exposed through a provisional CLI
command; issue #20 will define the final command surface.

## Build

TCC is the only supported compiler:

```text
make
```

The build treats every compiler warning as an error and produces `./cbs`.

## Test

```text
make test
```

The regression suite validates accepted and rejected definitions, diagnostic
shape and locations, UTF-8 handling, CRLF normalization, the `.cbs` extension,
and the guarantee that validation does not execute phases.

For TCC bounds instrumentation:

```text
tcc -b -Isrc -std=c11 -Wall -Wextra -Werror -pedantic \
    src/ast.c src/diag.c src/exec.c src/lexer.c src/main.c src/parser.c \
    src/validate.c \
    -o /tmp/cbs-bounds
./tests/parser-validation.sh /tmp/cbs-bounds
```

## Validate a package definition

```text
./cbs validate path/to/package.cbs
```

Successful validation prints one confirmation line and exits with status 0.
Recipe I/O, lexical, parse, and validation failures use status 3 and emit the
located diagnostic contract defined in
[`docs/spec/cpdl-0.1.md`](docs/spec/cpdl-0.1.md).

Validation is non-executing: it does not fetch sources, inspect the host
filesystem, resolve dependencies, or spawn phase commands.
