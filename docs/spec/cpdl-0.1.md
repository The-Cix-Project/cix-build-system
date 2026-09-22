# CPDL 0.1 language specification

- Status: Normative
- Version: 0.1
- Date: 2026-08-28
- File extension: `.cbs`

## 1. Purpose and conformance

The Cix Package Definition Language (CPDL) defines how CBS turns named,
verified inputs into a staged package tree. CPDL is deliberately not a general
programming language. CBS owns fetching, verification, sandbox policy,
normalization, manifest generation, packaging, and installation.

This document is normative for CPDL 0.1. The words **must**, **must not**,
**should**, and **may** carry their usual requirements meanings.

A conforming implementation must:

1. accept every document described as valid by this specification;
2. reject every document that violates a lexical, grammar, or validation rule;
3. validate a complete document without executing any phase;
4. execute commands without invoking a shell or shell-compatible parser; and
5. produce diagnostics and process exit statuses in the forms specified here.

CBS package definition files use only the `.cbs` extension, as decided by
ADR-0003. `.cpdl` is not an alias.

## 2. Source text and lexical grammar

### 2.1 Encoding and positions

A source file must be valid UTF-8. A byte-order mark is not permitted. Invalid
UTF-8 is a lexical error.

Line numbering and column numbering begin at 1. A column counts Unicode scalar
values, not UTF-8 bytes. A tab advances the column to the next 8-column tab stop
for diagnostic display, although tabs outside strings otherwise behave as
ordinary whitespace.

The line endings LF and CRLF are accepted. CBS normalizes CRLF to LF before
lexing. A bare CR is a lexical error.

### 2.2 Notation

The grammar uses this EBNF notation:

```text
name        = a grammar production
"text"      = literal source text
[ item ]    = zero or one item
{ item }    = zero or more items
item | item = alternatives
```

Whitespace and comments may appear between tokens unless a production says
otherwise. They are not permitted inside a token.

### 2.3 Whitespace and comments

```ebnf
whitespace = " " | "\t" | "\n" ;
comment    = "#", { any character except "\n" }, [ "\n" ] ;

digit         = "0"…"9" ;
nonzero-digit = "1"…"9" ;
octal-digit   = "0"…"7" ;
hex-digit     = digit | "A"…"F" | "a"…"f" ;
```

Comments have no semantic value. CPDL 0.1 has no block comments.

### 2.4 Identifiers and keywords

```ebnf
identifier       = identifier-start, { identifier-continue } ;
identifier-start = "A"…"Z" | "a"…"z" | "_" ;
identifier-continue = identifier-start | "0"…"9" | "-" ;
```

Identifiers are ASCII and case-sensitive. All keywords are lowercase. A
keyword cannot be used where the grammar requires an identifier.

The complete CPDL 0.1 keyword set is:

```text
allow_failure  after      any           architecture  as          bootstrap
build          cd         check         chmod       compiler       config
configure      contains   copy          count       directory      build_image
capability     toolchain  upstream      each        in
env            exactly    exit          exists      expect
extra          extract    file          from        glob          headers
insert         into       jobs           library     main
mkdir          move       on_fail        package     prepare
release        format     remove        replace     require     requires
run            runtime    sha256         source      sources     stage
symlink        target     test           timeout     to          tool
tree           until      url            version     whitespace  write
materialize    line
```

Keywords reserved for later versions are not silently accepted. An unknown
word is an identifier only in the few positions where this grammar explicitly
permits one.

### 2.5 Integers, modes, and durations

```ebnf
integer  = "0" | nonzero-digit, { digit } ;
mode     = "0", octal-digit, octal-digit, octal-digit
         | "0", octal-digit, octal-digit, octal-digit, octal-digit ;
duration = integer, ( "ms" | "s" | "m" | "h" ) ;
```

Integers are unsigned decimal values in the inclusive range 0 through
2,147,483,647. Leading zeroes are forbidden except for the value `0` and mode
tokens. Modes are octal permission bits from `0000` through `07777`; file-type
bits are forbidden.

A duration must be greater than zero. Its suffix means milliseconds, seconds,
minutes, or hours. Whitespace is not permitted between the integer and suffix.

### 2.6 Strings

#### Quoted strings

```ebnf
string = '"', { string-character | escape }, '"' ;
escape = "\\" ( '"' | "\\" | "n" | "r" | "t" | "0"
               | "x", hex-digit, hex-digit
               | "u", hex-digit, hex-digit, hex-digit, hex-digit ) ;

string-character = any Unicode scalar value except '"', "\\", "\n",
                   control characters, and NUL ;
block-string = opening-block-delimiter, block-content,
               closing-block-delimiter ;
opening-block-delimiter = '"""', "\n" ;
closing-block-delimiter = line-indentation, '"""', [ "\n" ] ;
```

`block-content` and `line-indentation` are governed by the contextual indentation
rules below; they cannot be expressed by context-free EBNF alone.

An unescaped newline or control character is forbidden inside a quoted string.
NUL is not allowed in source text. The `\0` and `\x00` escapes are also forbidden
because CPDL strings must be representable as process arguments and filesystem
paths. A `\u` escape that denotes a surrogate or zero is invalid.

Unknown escapes are lexical errors. In particular, `\$` is invalid and is not
needed: a dollar sign has no special meaning unless it begins the exact
interpolation syntax below.

#### Block strings

A block string is intended for generated files and source-edit text:

```text
"""
first line
second line
"""
```

The opening delimiter must be followed immediately by LF. The closing `"""`
must be the only non-whitespace text on its line. Its indentation prefix is
removed from every non-blank content line; a non-blank line with less indentation
is a lexical error. Blank lines have all indentation removed. The LF immediately
before the closing delimiter is part of the value. No escapes or interpolation
are processed inside a block string. The three-byte sequence `"""` cannot occur
in its content.

#### Interpolation

Quoted strings, but not block strings, support explicit CBS interpolation:

```text
${name}       ${version}    ${release}    ${arch}
${src}        ${build}      ${dest}       ${jobs}       ${triplet}
${firmware}   ${source.gmp} ${stdout.NAME}
```

Only the exact `${...}` form interpolates. `$`, `$name`, `$(command)`, shell
metacharacters, quotes, spaces, `*`, and `;` are ordinary string bytes. A `${`
sequence that does not name an allowed CBS value is a validation error.

Interpolation produces one value and never causes word splitting or globbing.
For example, `"--prefix=${dest}/usr tree"` remains one argument even when its
result contains spaces.

### 2.7 CBS values

```ebnf
cbs-value = "$name" | "$version" | "$release" | "$arch" | "$triplet"
          | "$src" | "$build" | "$dest" | "$jobs" | "$firmware"
          | "$source.", identifier ;
value     = string | block-string | cbs-value | integer ;
text-value = string | block-string | cbs-value ;
path-value = string | cbs-value ;
```

A bare CBS value is a typed value, not shell syntax. `$release` and `$jobs` are
integers; the other supplied values are strings. `$triplet` is a target value
derived from `$arch` using CBS's platform mapping; it is empty when the target
architecture has no registered libc mapping. `$firmware` is the caller-supplied
firmware tree from `--firmware-root`; it is empty when the caller supplied
none, so a recipe that needs it must assert on it. `$source.NAME` is valid only when
`NAME` names a source declared in the same package.

## 3. Document grammar

### 3.1 Top level

```ebnf
document = package-declaration, end-of-file ;

package-declaration = "package", string, "{",
                      { package-item },
                      "}" ;

package-item = version-declaration
             | release-declaration
             | format-declaration
             | license-declaration
             | sources-declaration
             | requires-declaration
             | build-image-declaration
             | capability-declaration
             | toolchain-declaration
             | upstream-declaration
             | metadata-declaration
             | prepare-phase
             | configure-phase
             | build-phase
             | check-phase
             | install-phase ;

version-declaration      = "version", string ;
release-declaration      = "release", integer ;
format-declaration       = "format", string ;
license-declaration      = "license", string ;
build-image-declaration  = "build_image", string ;
capability-declaration   = "capability", string ;
toolchain-declaration    = "toolchain", string, "{", "reason", string, "}" ;
upstream-declaration     = "upstream", string ;
metadata-declaration     = "metadata", "{", { string, string }, "}" ;
```

`build_image` and `capability` are execution metadata consumed by the build
orchestrator; standalone CBS records and validates them but cannot create an
image or grant a capability. A non-TCC compiler requires a matching
`toolchain` declaration with a non-empty reason. GCC is currently the only
permitted exception to the TCC compiler policy.
`upstream` identifies a registered release-discovery provider; CPDL 0.1
currently registers `kernel.org`, while the declared source URL and digest
remain the immutable build input until a resolver selects a new release.

A document contains exactly one package declaration and no trailing tokens.
Semicolons and commas are not part of CPDL.

Builds set `SOURCE_DATE_EPOCH=0` in every child process. CIXPKG manifests and
payloads are emitted in canonical path order with normalized ownership; CBS
does not claim that arbitrary toolchains produce bit-identical output, but it
provides this stable epoch and deterministic packaging boundary for tools that
honor it.

CIXPKG manifests also record provenance metadata: the recipe path and digest,
CBS version, architecture, declared toolchain, and each declared source's
name, first URL, and SHA-256. The recipe digest covers the published recipe
bytes supplied to CBS.

`license` is an optional SPDX expression carried into the artifact manifest as
an `m license <expression>` line; it must be a non-empty single-line string.
`metadata { "key" "value" ... }` is an optional block of opaque string pairs
that CBS carries but never interprets: no key is reserved and no value affects
validation, execution, or identity.

The package name, version, release, and artifact format are required. CBS supplies the build
target architecture; a CPDL 0.1 recipe cannot select or override it. The
`architecture` and `any` keywords remain reserved for a future decision about
architecture-independent packages, but an architecture declaration is invalid
in CPDL 0.1.

CBS constructs one canonical identity tuple `(name, version, release,
architecture)`. Its canonical text is
`name-version-release-architecture`, and the artifact filename is that text
plus `.cixpkg`. Artifact metadata and its digest input contain the same
canonical text rather than independently reconstructing identity fields.

Each package-level declaration may appear at most once, except that
`capability` may be repeated. Package items must appear in the canonical order
shown by `package-item`: identity, upstream, sources, requirements, execution
metadata, then the five phases. An omitted optional item does not affect the
order of later items.

Package names must match:

```text
[a-z0-9][a-z0-9+.-]*
```

They may not be `.` or `..`. Versions are non-empty UTF-8 strings without NUL,
`/`, or ASCII whitespace. Release must be greater than zero.

### 3.2 Sources

```ebnf
sources-declaration = "sources", "{", source-declaration,
                      { source-declaration }, "}" ;

source-declaration = source-kind, string, "{",
                     source-url, { source-url }, source-sha256,
                     "}" ;

source-kind   = "main" | "extra" ;
source-url    = "url", string ;
source-sha256 = "sha256", string ;
```

A `sources` block contains exactly one `main` source and zero or more `extra`
sources. Source names are unique identifiers expressed as strings; after escape
processing they must match the package-name pattern above. Source declarations
must place `url` before `sha256`, each exactly once.

URLs are ordered mirrors for one source identity and share its one paired
checksum. URLs must be absolute and use a scheme enabled by CBS policy. The grammar does
not select permitted network schemes. A SHA-256 value must contain exactly 64
lowercase hexadecimal digits.

CBS fetches and verifies all declared sources. It extracts the main source into
`$src` when it is a supported archive. A verified main source that is not an
archive is preserved byte-for-byte at `$src/<name>/<basename>` instead. Extra
sources remain named verified inputs available as `$source.NAME` until an
explicit `extract` or `materialize` operation uses them.

No `$source.NAME` bindings are exposed until every declared source has been
verified. A mismatch names the source and both expected and computed digests.

### 3.3 Dependencies

```ebnf
requires-declaration = "requires", "{", { dependency-group }, "}" ;

dependency-group = dependency-role, "{", { dependency }, "}" ;
dependency-role  = "build" | "runtime" | "test" | "bootstrap" ;

dependency      = dependency-kind, string ;
dependency-kind = "tool" | "library" | "headers" | "compiler" | "package" ;
```

Each role may occur at most once and groups must appear in the role order shown
above. A dependency tuple of role, kind, and name must be unique. Dependency
names follow the package-name pattern.

The meanings are:

- `build`: required throughout package construction, including `check`;
- `runtime`: required for the installed package's intended function;
- `test`: added only for `check`;
- `bootstrap`: a compiler-lineage seed requirement.

The only external compiler dependency permitted by CPDL 0.1 is
`compiler "tcc"`. A different compiler name in any dependency role is a
validation error. A compiler produced inside a TCC-rooted build may be invoked
by later phases without becoming an external dependency.

## 4. Phases and operations

### 4.1 Phase grammar

```ebnf
prepare-phase   = "prepare", operation-block ;
configure-phase = "configure", operation-block ;
build-phase     = "build", operation-block ;
check-phase     = "check", operation-block ;
install-phase   = "install", operation-block ;

operation-block = "{", { operation }, [ on-fail ], "}" ;

operation = run-operation
          | cd-operation
          | environment-operation
          | mkdir-operation
          | copy-operation
          | move-operation
          | remove-operation
          | symlink-operation
          | write-operation
          | chmod-operation
          | extract-operation
          | materialize-operation
          | replace-operation
          | insert-operation
          | truncate-operation
          | glob-binding-operation
          | links-operation
          | patch-operation
          | require-operation
          | each-operation
          | stage-operation ;

on-fail = "on_fail", "{", { diagnostic-operation }, "}" ;

diagnostic-operation = run-operation
                     | require-operation ;
```

Each phase is optional and may occur at most once. Present phases appear in the
fixed order `prepare`, `configure`, `build`, `check`, `install`. Operations
execute in source order.

An `on_fail` block may occur only once and only as the final member of its phase
or `cd` block. It executes only after an operation in its associated block
fails. It never changes the original failure into success. A failing diagnostic
is reported as a note attached to the original failure.

The environment at package start is constructed by CBS policy, not inherited
implicitly from the invoking process. A phase receives a copy of that environment.
CBS creates `$src`, `$build`, and `$dest` before phase execution. Every phase
begins with `$build` as its directory context, so an out-of-tree build is the
default; a recipe that must work inside the extracted tree selects it
explicitly with `cd "${src}/NAME" { ... }`.

### 4.2 `run`

```ebnf
run-operation = "run", text-value, "{", { run-item }, "}" ;

run-item = argument
         | run-environment
         | run-jobs
         | run-timeout
         | run-expect
         | run-output-assert
         | run-output-bind
         | run-output-file
         | "allow_failure" ;

argument        = text-value ;
run-environment = "env", string, "=", text-value ;
run-jobs        = "jobs", ( integer | "$jobs" ) ;
run-timeout     = "timeout", duration ;
run-expect      = "expect", "exit", integer ;
run-output-assert = "expect", "{", ( "stdout" | "stderr" ),
                     "contains", string, "}" ;
run-output-bind = ( "stdout" | "stderr" ), string ;
run-output-file = ( "stdout" | "stderr" ), "file", path-value ;
run-each        = "each", { text-value } ;
```

The first value names the executable. Each bare `text-value` in the block adds
exactly one argument in source order. The executable and arguments must not
contain NUL.

CBS constructs `argv` as:

```text
argv[0] = resolved executable value
argv[1..n] = resolved argument values
argv[n+1] = NULL
```

CBS passes this vector directly to the operating-system process execution
interface. It must not invoke `sh`, `bash`, `env`, or another command
interpreter on the recipe's behalf. CBS performs no shell parsing, field
splitting, command substitution, pipeline construction, or implicit glob
expansion.

A recipe may execute a verified upstream script such as `./configure`; in that
case the script is a named source input and the kernel honors its interpreter
header. A recipe must not name a command interpreter as a `run` executable to
embed recipe logic in an argument. CBS validation rejects the literal basenames
`sh`, `bash`, `dash`, `ash`, `ksh`, `zsh`, and `env`. CBS runtime validation
applies the same check to the resolved executable name. This rule prevents a shell
escape in CPDL while allowing upstream build systems that are themselves
scripts.

The executable is resolved using the phase environment's deterministic `PATH`
when it contains no `/`. A value containing `/` is resolved relative to the
current CPDL directory context. CBS uses `execve` after resolution; `execvp`,
`system`, and `popen` are not conforming execution paths because they admit
ambient environment or shell behavior.

`stdout` and `stderr` assertions, bindings, and file captures observe their
respective process streams; they are never implicitly merged. Captured output
is bounded at 64 KiB; exceeding it fails with `CPDL-E4001` and does not create
a partial artifact. `stdout "NAME"` and `stderr "NAME"` expose one-line,
trimmed values as `${stdout.NAME}` and `${stderr.NAME}` for later commands.
The file forms preserve bounded bytes at a confined path and are not general-
purpose output redirects; commands that produce large files should write them
directly or use a filesystem operation after producing a declared source.

`env "NAME" = value` creates or replaces one command-local environment binding.
It does not affect later commands. Names are POSIX portable names in either
case, `[A-Za-z_][A-Za-z0-9_]*` — lowercase autoconf and libtool cache
variables such as `ac_cv_func_*` are ordinary names — and `=`, NUL, and
punctuation are refused; duplicate names in one command are validation
errors. Values are passed as literal bytes
after CBS interpolation.

`jobs N` declares the maximum concurrency the command may use. `N` must be
positive and must not exceed `$jobs`; `$jobs` selects the CBS policy value. This
metadata does not alter `argv` and does not invent a tool-specific flag. A build
tool receives its jobs flag through an explicit argument such as
`"-j${jobs}"`. CBS may use the declaration for resource accounting.

At most one each of `jobs`, `timeout`, `expect`, and `allow_failure` may occur.
The default expected exit status is 0. An expected status must be between 0 and
255 inclusive. A non-matching status or terminating
signal fails the operation. `allow_failure` records the result but permits the
next operation to run; inside `on_fail` it applies to the diagnostic block.
A trailing `allow_failure` on `copy` or `remove` records a filesystem failure
and permits the next operation to run. It is never implicit and cannot hide a
failure on an unmarked operation.

A timeout terminates the complete CBS-created process group, waits for it to be
reaped, and fails the operation. The grace period and signal sequence are CBS
policy and must appear in the diagnostic.

Every directly executed child also receives resource ceilings before `execve`:
address space, individual file size, CPU time, open file descriptors, and
process count. Standalone defaults are 8192 MiB, 16384 MiB, 24 hours, 4096
descriptors, and 4096 processes respectively. An embedder may provide lower
ceilings through the execution context. A command timeout also supplies a
matching CPU-time ceiling when no explicit CPU limit is provided. CBS reports
the command failure if a limit is exceeded. These are per-process limits, not a
filesystem quota; hosted builds must use their container/cgroup disk and
memory budgets for aggregate workspace enforcement.

If the CBS runner receives SIGINT while a command is active, it forwards the
interrupt to the complete CBS-created process group, escalates to SIGKILL if
necessary, reaps the child, and fails the operation. No child is left behind
by timeout or interactive interruption.

### 4.3 Environment and directory scope

```ebnf
environment-operation = "env", string, "=", text-value ;
cd-operation          = "cd", path-value, operation-block ;
```

A block-level `env` binding applies from its declaration through the remainder
of the current lexical block, including nested `cd` blocks. A nested binding
shadows the outer value and the outer value is restored on leaving the block.
Environment-name validation is the same as for command-local bindings.

`cd` resolves its path in the current directory context and executes its block
with that directory as the context. It does not change the CBS process working
directory globally. Leaving the block restores the prior context even on failure.

### 4.4 Filesystem operations

```ebnf
mkdir-operation   = "mkdir", path-value, [ "chmod", mode ] ;
copy-operation    = "copy", [ "tree" ], source-selector, "to", path-value,
                     [ "allow_failure" ] ;
move-operation    = "move", source-selector, "to", path-value ;
remove-operation  = "remove", [ "tree" ], source-selector,
                     [ "allow_failure" ] ;
symlink-operation = "symlink", path-value, "to", path-value ;
write-operation   = "write", path-value, text-value, [ "chmod", mode ] ;
chmod-operation   = "chmod", mode, source-selector ;

source-selector = path-value | "glob", string ;
```

Paths are interpreted by CBS, never by a shell. A glob selector is evaluated by
CBS using the CPDL glob rules in section 4.7. A non-glob selector always denotes
one literal path, even when it contains `*`, `?`, or `[`.

`mkdir` creates all missing path components. Existing directories are accepted;
an existing non-directory fails. Its default final mode is `0755`, filtered only
by explicit CBS policy recorded in build metadata.

`copy` preserves file bytes and permission bits. It does not preserve numeric
ownership: new regular files are owned by the CBS build identity, and staged
ownership is assigned later by CBS package policy. A directory source is
invalid unless `copy tree` is used. `copy tree SRC to DEST` recursively copies
the contents of the confined source directory into the confined destination,
preserving regular-file modes and symbolic links without dereferencing them.
The tree form requires one literal directory source and creates the destination
directory when absent. Multiple glob matches
require an existing directory destination. A single source follows the same
destination naming rules as POSIX `cp` without dereferencing a source symlink.
An existing regular-file destination is replaced. An existing destination
symlink is replaced rather than followed, so it cannot redirect a write outside
the CBS roots. An existing directory is accepted only as the destination
container; other destination types fail.

`move` is confined to one staged build filesystem and must not silently fall
back to copy-and-delete across filesystems. It uses the same destination naming
rules as `copy`; an existing non-directory destination is atomically replaced,
while replacement of a non-empty directory fails. `remove` removes files,
empty directories, and symlinks. Removing a non-empty directory requires
`tree`; recursive removal never traverses a symlinked directory.

File requirements may assert `exists`, `nonempty`, `executable`, `contains`,
and `same_as`.
`nonempty` requires a regular file with at least one byte; `contains` checks
for a byte string; `executable` requires at least one execute bit; and `same_as`
compares the complete file contents.

`symlink TARGET to LINK_PATH` creates `LINK_PATH` with the exact target bytes.
CBS does not canonicalize the target. An existing destination fails.

`write` creates or replaces one regular file atomically within the build
filesystem. Its default mode is `0644`. `chmod` changes permission bits without
following a final symlink.

Filesystem operations fail on zero glob matches unless their grammar includes
an explicit cardinality assertion that permits zero. CPDL 0.1 provides no
force, ignore-missing, or overwrite switch; accepted overwrite behavior is
defined explicitly above.

### 4.5 Extraction

```ebnf
extract-operation = "extract", source-reference,
                    "into", path-value,
                    [ "as", string ] ;

materialize-operation = "materialize", source-reference,
                        "to", path-value ;

source-reference = "$source.", identifier ;
```

`extract` accepts only a declared named source. CBS verifies the source before
phase execution. It extracts beneath the `into` directory using the supported
archive-format policy. `as "NAME"` requires the archive to contain one logical
top-level directory and renames that directory to `NAME` after safe extraction.

`materialize` accepts only a declared, verified source whose bytes are not an
archive. It copies that exact regular file into the confined build filesystem;
it cannot read an arbitrary cache path or follow a source symlink. This is
intended for checked configuration fragments and other auxiliary source files.

Configuration assertions use `require config PATH { SYMBOL = STATE ... }`.
`STATE` is `y`, `m`, `n`, or `absent`; `n` accepts either `SYMBOL=n` or Linux's
`# SYMBOL is not set` spelling, while `absent` requires neither form.

Absolute archive paths, `..` traversal, embedded NUL, duplicate output paths,
and entries escaping through symlinks are runtime failures. A member whose
parent directory has no member of its own is extracted beneath implicitly
created parents; a directory member applies its mode and modification time
after every member beneath it has been written, whatever the archive order.
An extraction failure names the source, the member, and the rule or
operating-system error that stopped it. Exact archive formats and additional
metadata rules are decided separately.

### 4.6 Source edits

```ebnf
replace-operation = "replace", source-edit-target, "{",
                    "from", text-value,
                    [ "until", ( "whitespace" | "line" ) ],
                    "to", text-value,
                    "exactly", integer,
                    "}" ;

insert-operation = "insert", source-edit-target, "{",
                   ( "before" | "after" ), text-value,
                   "write", text-value,
                   "exactly", integer,
                   "}" ;

truncate-operation = "truncate", path-value, "{", "from", text-value,
                     "exactly", integer, "}" ;

glob-binding-operation = "glob", string, "=", string,
                         [ "exactly", integer ] ;

links-operation = "links", path-value, "{", {
                  ( "needs" | "forbids" ), string }, "}" ;

patch-operation = "patch", string, "{", "sha256", string,
                  [ "strip", integer ], "}" ;

source-edit-target = path-value | "glob", string ;
```

`replace` counts non-overlapping byte-for-byte matches of `from`. The count must
equal `exactly` before any mutation occurs. For a glob target the count is the
total across all sorted matches; zero matching paths fails. An empty `from` is
invalid.

With `until`, each match is the literal `from` plus every following byte up
to, but not including, the first delimiter or the end of the file: `until
whitespace` stops at a space, tab, CR, or LF; `until line` stops at CR or LF.
This removes or rewrites a flag together with its argument
(`from "-Wl,--version-script=" until whitespace to ""`) without a pattern
language; the delimiter is never part of the match and the count rule is
unchanged, so an edit that matches nothing still fails. `insert` has no
`until`.

`insert` counts non-overlapping byte-for-byte matches of its anchor. The count
must equal `exactly` before mutation. `after` inserts `write` immediately after
each matched byte sequence; `before` inserts it immediately before each match.
An empty anchor is invalid.

Both operations read and replace a regular file atomically, preserve its mode,
and fail without modifying it when validation, counting, reading, or writing
fails. CPDL 0.1 source edits are byte operations; they do not implement regular
expressions or locale-dependent text matching.

`truncate` reads a regular file and, after its exact cardinality check, keeps
the bytes before the one literal `from` match and discards that marker and all
following bytes. It preserves the file mode and commits atomically; a failed
count or filesystem operation leaves the original unchanged.

`patch` reads a unified-diff file from the recipe directory, verifies its
declared SHA-256, and applies exact-context hunks to the source tree. Context
must match byte-for-byte, with no fuzz or reject files; a failed digest,
context check, or write leaves the source unchanged. `strip` removes that
many leading path components from the patch's `+++` path.

`glob "NAME" = PATTERN` resolves a confined glob in sorted path order and
binds its match as `${glob.NAME}` for later operations. It requires exactly
one match by default; `exactly N` permits an explicit multi-match count but
still binds the first sorted match. A count mismatch or filesystem failure
does not publish a binding.

### 4.7 Assertions and globs

```ebnf
require-operation = require-file
                  | require-directory
                  | require-symlink
                  | require-glob ;

require-file = "require", "file", path-value, "{",
               "exists",
               { "nonempty" | "executable" | "contains", text-value
               | "same_as", path-value },
               "}" ;

require-directory = "require", "directory", path-value, "{",
                    "exists",
                    "}" ;

require-symlink = "require", "symlink", path-value, "{",
                  "exists",
                  [ "target", text-value ],
                  "}" ;

require-glob = "require", "glob", string, "{",
               ( "exactly" | "count" ), integer,
               "}" ;
```

Any require operation may be followed by `for { text-value ... }`. Each value
expands the assertion into one independent operation. A `run` may contain
`each text-value ...` after its fixed arguments; each value similarly expands
the command, appending that value as the final argument. Both lists must be
non-empty, and expansion is syntactic: there are no runtime variables or
control flow. The `each` operation (§4.8) is the same mechanism with a bound
placeholder name and a compound body.

`require file` follows no final symlink and requires a regular file. `nonempty`
requires that file to contain at least one byte.
`require directory` requires a directory and accepts no other property.
`require symlink` requires a symbolic link and does not follow it, so a
dangling link satisfies `exists`; `target` compares the link text literally,
after substitution, with the value given. Each `contains` performs a literal
byte search. `same_as` compares two confined regular files byte-for-byte.
`require glob` requires exactly the stated number of matches.

A failed `require file`, `require directory`, or `require symlink` names the
path and what was found there: a symbolic link, a directory, a regular file,
an unreadable entry, or a parent that is not a directory. `does not exist` is
reported only for a path that is absent.

CPDL globs recognize:

- `?` for one non-`/` byte;
- `*` for zero or more non-`/` bytes;
- `**` as a complete path component for zero or more path components;
- `[abc]` and `[a-z]` byte classes; and
- `[!abc]` negated byte classes.

Backslash quotes the next glob byte. An unterminated or empty class is a
validation error. Matching is bytewise and case-sensitive. Results are sorted
by unsigned UTF-8 byte order before an operation observes them. Hidden path
components are not special. A glob never traverses a symlinked directory.

### 4.8 Iteration

```ebnf
each-operation = "each", string, "in", "{", string, { string }, "}",
                 operation-block ;
```

`each` binds a name to each quoted item in turn and expands its body once per
item, in order, at parse time. Inside the body, `${each.NAME}` in any quoted
string is replaced by the item before interpolation and validation, so the
bound item is usable wherever a string value is: paths, `run` arguments,
written text, `require` targets, and `env` values. Block strings do not
interpolate and are left unchanged; the bare form `$each.NAME` is not a value.
Items are quoted strings and may themselves interpolate.
The bound value also supports the lexical accessors `${each.NAME.basename}`,
`${each.NAME.dirname}`, and `${each.NAME.stem}`. `basename` is the final path
component, `dirname` is the preceding path (or `.` when there is none), and
`stem` removes the final extension from the basename (except for a leading
dotfile). These accessors do not access the filesystem.

Each expansion is an independent block: an `env` binding made inside it ends
with that item, and an `on_fail` at the end of the body runs for the item that
failed. The first failing item stops the phase; after the operation's own
diagnostic, `CPDL-N4002` names the bound name, the item ordinal, and the item.
Bodies may nest `each` with distinct names. An item list and a body must be
non-empty (`CPDL-E2003`). The bound name is a parse-time placeholder, not a
runtime variable: `explain` counts the expanded operations, and there is no
runtime state, condition, or loop.

### 4.9 Staging sandbox libraries

```ebnf
stage-operation = "stage", "library", string, "into", path-value ;
```

`stage library` copies one shared library out of the build sandbox into the
staged tree. The name is a bare file name (no `/`), such as `libresolv.so.2`.
CBS searches, in order, `/usr/lib/TRIPLET`, `/lib/TRIPLET`, `/usr/lib`,
`/lib`, `/usr/lib64`, and `/lib64`, where `TRIPLET` is `${triplet}` for the
target architecture, and takes the first regular file or symbolic link found.
The copy preserves the mode, and a symbolic link is copied as a link with its
target text unchanged, so the soname link a recipe names is shipped as a link
and the versioned file must be staged separately. The `into` directory must
resolve beneath a confined root; it is created (mode 0755) when absent and
left untouched when present. When no directory holds the library, the
operation fails with `CPDL-E4004`, names the library and every directory
searched, and asks for the build dependency that provides it to be declared.

This is the one operation whose source lies outside the confined roots. The
candidate directories are the build image's library layout, never a
recipe-supplied path (ADR-0036).

## 5. Validation contract

Parsing constructs a complete syntax tree without executing operations.
Validation is a separate pass over that tree. A command that validates a file
must not fetch sources, inspect the host filesystem, resolve dependencies, spawn
processes, or run phase operations.

Validation checks every statically decidable rule, including:

- required, unique, and canonically ordered declarations;
- package, source, dependency, and environment names;
- source uniqueness and exactly one main source;
- URL structure and SHA-256 spelling;
- phase uniqueness and order;
- dependency group uniqueness and TCC bootstrap policy;
- the prohibition on command interpreters as `run` executables;
- operation placement, option uniqueness, and typed values;
- references to declared sources;
- variable availability and interpolation spelling;
- integer, mode, duration, and expected-exit ranges;
- non-empty source-edit needles;
- glob syntax; and
- path rules that can be checked without accessing the filesystem.

Validation reports all independent errors it can safely find in source order.
A parse error may prevent recovery; recovery rules are implementation details,
but CBS must never execute a partially parsed or invalid document.

## 6. Path and value safety

Source paths are UTF-8 strings without NUL. CBS normalizes repeated `/` and `.`
components for resolution but retains the original spelling for diagnostics.
`..` may not escape the current phase root. Absolute paths are permitted only
when they begin with a CBS-supplied root (`$src`, `$build`, or `$dest`) after
interpolation. This is checked again at operation time against filesystem links.

The value `$dest` is available in all phases but writes outside phase-appropriate
roots may be rejected by CBS policy. Package creation observes only `$dest`.

CBS-supplied values are immutable. CPDL 0.1 has no user variables, arithmetic,
conditionals, loops, functions, imports, or includes.

## 7. Failure model

Every operation either succeeds or returns one structured failure. A phase stops
at its first non-allowed failure. CBS retains that failure while running the
associated `on_fail` diagnostics, then returns the original failure.

Process failure records at least:

- executable as declared and resolved;
- phase and operation location;
- exit status or terminating signal;
- timeout information when applicable; and
- captured-log location when logging is enabled.

Filesystem failure records the CPDL operation, logical path, and operating-system
error. CBS must not reinterpret an error as success based on later diagnostic
operations.

## 8. Diagnostic contract

### 8.1 Primary form

Every diagnostic begins with exactly this machine-recognizable line:

```text
PATH:LINE:COLUMN: SEVERITY[CODE]: CATEGORY: MESSAGE
```

Where:

- `PATH` is the recipe path as passed to CBS, or `<command-line>`;
- `LINE` and `COLUMN` identify the first source token responsible;
- `SEVERITY` is `error`, `warning`, or `note`;
- `CODE` is a stable identifier described below;
- `CATEGORY` is `lex`, `parse`, `validation`, `runtime`, or `internal`; and
- `MESSAGE` is one complete sentence without a trailing period.

CBS emits source context on following lines when source is available:

```text
  LINE | source text
       |     ^~~~
```

Related information uses additional `note[...]` diagnostics. Diagnostics go to
standard error. Normal command output goes to standard output.

### 8.2 Code ranges

```text
CPDL-E1xxx  lexical errors
CPDL-E2xxx  parse errors
CPDL-E3xxx  validation errors
CPDL-E4xxx  runtime/operation errors
CPDL-E5xxx  source preparation and verification errors
CPDL-E9xxx  internal invariant failures
CPDL-W3xxx  validation warnings
CPDL-Nxxxx  related notes
```

Once assigned, a code retains its meaning throughout CPDL 0.x. New diagnostics
receive new codes; code reuse is forbidden.

The initial mandatory codes are:

| Code | Meaning |
| --- | --- |
| `CPDL-E1001` | Invalid UTF-8 or line ending |
| `CPDL-E1002` | Invalid character or token |
| `CPDL-E1003` | Invalid or unterminated string |
| `CPDL-E2001` | Expected token was not present |
| `CPDL-E2002` | Unexpected token or trailing input |
| `CPDL-E2003` | Invalid each declaration |
| `CPDL-E3001` | Missing required declaration |
| `CPDL-E3002` | Duplicate declaration or option |
| `CPDL-E3003` | Declaration appears out of order |
| `CPDL-E3004` | Invalid name or literal value |
| `CPDL-E3005` | Unknown or invalid reference |
| `CPDL-E3006` | Operation is invalid in this context |
| `CPDL-E4001` | Process exited with an unexpected status |
| `CPDL-E4002` | Process terminated by a signal |
| `CPDL-E4003` | Process timed out |
| `CPDL-E4004` | Filesystem operation failed |
| `CPDL-E4005` | Assertion or cardinality check failed |
| `CPDL-E4006` | Source extraction failed safety validation |
| `CPDL-E5001` | Source checksum verification failed |
| `CPDL-E9001` | CBS internal invariant failed |

### 8.3 Required wording examples

Parse error:

```text
zlib.cbs:7:5: error[CPDL-E2001]: parse: expected `sha256`, found `}`
  7 |     }
    |     ^
```

Validation error:

```text
gcc.cbs:24:9: error[CPDL-E3005]: validation: source `mpc` is not declared
  24 |         extract $source.mpc into $src
     |         ^~~~~~~
```

Runtime process failure:

```text
zlib.cbs:31:9: error[CPDL-E4001]: runtime: `make` exited with status 2; expected 0
  31 |         run "make" {
     |         ^~~
```

The wording before interpolated names and numbers is stable. Operating-system
error text may follow after a semicolon and is locale-independent: CBS uses its
own English mapping or includes the numeric error value.

### 8.4 Internal failures

An internal invariant failure is never reported as invalid recipe input. CBS
emits `CPDL-E9001`, identifies its own source location when built with diagnostic
metadata, and exits without executing further operations. It must not crash
silently or continue with a partial package.

## 9. CBS process exit statuses

```text
0   requested operation completed successfully
2   invalid CBS command-line usage
3   recipe, source-preparation, or build/runtime failure
4   artifact verification or extraction failure
```

These are the statuses ADR-0008 fixes and `tests/cli-contract-test.sh`
asserts. A recipe failure and a phase failure share status 3: the category is
carried by the diagnostic code, not the process status. Statuses 5, 6, and 70
appeared in an earlier draft of this section and were never implemented; they
are not reserved and must not be assumed.

CBS returns the category status, not the raw child status. The raw exit status
or signal remains present in the structured diagnostic and build record.

## 10. Explicit exclusions from CPDL 0.1

CPDL 0.1 has no:

- implicit or unsafe shell escape;
- pipelines, redirections, command substitution, or shell operators;
- user-defined functions, types, classes, or modules;
- imports or includes;
- general variables or assignment — `${each.NAME}` (§4.8) and `${stdout.NAME}`
  (§4.2) are not variables: the first is a parse-time placeholder substituted
  into the expanded body, and the second is an immutable binding of one
  captured command result;
- arithmetic or boolean expressions;
- conditionals, loops, or arbitrary control flow — `each` (§4.8) expands its
  body once per literal item while the recipe is parsed, so the operation list
  is fixed before execution and `explain` can count it; there is no runtime
  iteration, test, or branch;
- regular expressions;
- version-constraint expressions or dependency solver syntax;
- package feature/options matrix;
- package-container or compression scripting;
- network operations inside phases; or
- native `helper` operation; any exceptional helper must satisfy ADR-0004 and
  appear in the native-helper registry.

Unknown constructs must fail parsing or validation. They must never be forwarded
to a shell or ignored for compatibility.

## 11. Complete structural example

This example demonstrates the complete shape, not a final zlib recipe or a
promise that the shown checksum is current:

```cpdl
package "zlib" {
    version "1.3.1"
    release 1

    sources {
        main "zlib" {
            url "https://zlib.net/zlib-1.3.1.tar.xz"
            sha256 "0000000000000000000000000000000000000000000000000000000000000000"
        }
    }

    requires {
        build {
            compiler "tcc"
            tool "make"
        }
    }

    configure {
        run "./configure" {
            "--prefix=/usr"
            env "CC" = "tcc"
            timeout 2m
        }
    }

    build {
        run "make" {
            "-j${jobs}"
            jobs $jobs
        }
    }

    check {
        run "make" {
            "check"
            timeout 5m
        }
    }

    install {
        run "make" {
            "install"
            "DESTDIR=${dest}"
        }

        require file "${dest}/usr/lib/libz.a" {
            exists
        }
    }
}
```

## 12. Implementation gates

The parser implementation for issue #2 must include positive and negative
fixtures covering every production and validation rule in this document.

The `run` implementation for issue #3 must include a child fixture that records
its received argument vector and environment without interpretation. The test
must pass arguments containing spaces, single and double quotes, `$`, `*`, `;`,
backslashes, and an empty string, then compare every received byte with the
expected value. The process path must be inspected or instrumented sufficiently
to prove that no shell function is reachable from CPDL execution.

No later implementation issue may introduce syntax by accident. A new construct
requires a specification change and, when architectural, an ADR before code.
