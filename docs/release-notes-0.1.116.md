# CBS v0.1.106 – v0.1.116, and the licence change

For Cix maintainers, recipe authors, and anyone embedding CBS. It covers the
licence decision of 2026-10-23 and the eleven releases of 2026-10-09, which
closed fourteen tracker issues.

Read the two **Act on this** items if you read nothing else.

## Licence and marks

CBS is free software under the **GNU Lesser General Public License, version 3,
or at your option any later version** (`LGPL-3.0-or-later`). The owner's
decision of 2026-10-23.

- [`COPYING.LESSER`](../COPYING.LESSER) is the LGPLv3 and
  [`COPYING`](../COPYING) the GPLv3 it adds permissions to. Both texts are in
  the repository, verbatim, because the LGPL is defined as a set of additional
  permissions on top of the GPL rather than a standalone licence.
- The Cix name and marks are governed **separately**, by
  [`TRADEMARK.md`](../TRADEMARK.md), which also states what counts as an
  official Cix artifact. A licence grants no right to the marks.
- Neither a licence nor the marks policy grants infrastructure or support.

Nothing in the documentation is a licence term. Where a guide or a
specification and a licence text disagree, the licence text governs.

## Act on this

### 1. A `write` that fails now fails the build

Until v0.1.109, **every** `write` failure was reported as success. The build
exited 0, emitted no diagnostic, and produced a verified CIXPKG **silently
missing the file**.

The cause was an overloaded AST field: `second_flag` carries a value's token
kind, and the filesystem failure path read it as `allow_failure`. A quoted
string is a nonzero token kind, so every failing `write` looked permitted.

**What this means for you.** This does not break a correct recipe. It exposes
one that was already broken. A recipe whose `write` has been failing — a
read-only destination, a path collision, a full filesystem — built "green" and
shipped an incomplete package; it now fails loudly with `CPDL-E4004` naming the
path. If a build goes red on upgrade, the artifact it produced before was
wrong.

Worth re-checking any package whose contents have ever been unexpectedly
missing a file.

### 2. `allow_failure` on `move` is now rejected

CPDL 1.0 §4.4 grants `allow_failure` to `copy` and `remove` only. The parser
had been accepting it on `move` as well, and the flag was live at runtime. From
v0.1.111 it is a validation error:

```text
error[CPDL-E3004]: validation: move does not support allow_failure
```

The asymmetry is deliberate rather than an oversight. `copy` and `remove` are
best-effort over paths that may legitimately be absent; a `move` renames
something the recipe has just produced, so tolerating its failure hides a
defect.

**Measured before changing it, and re-measured on 2026-10-30:** across the
corpus, `move` appears 4 times and **none** of those uses `allow_failure`. This
breaks no recipe that exists.

## Unblocked

### edk2, OVMF and qemu are extractable

One escaping symlink used to refuse an entire source archive. edk2 carries
exactly one — `EmulatorPkg/Unix/Host/X11IncludeHack` → `/opt/X11/include`, a
dangling macOS path in a subtree no consumer compiles — and qemu vendors the
same file under `roms/`. Both packages were unbuildable, and the refusal
aborted mid-extraction, leaving a partial `$src`.

From v0.1.106, a symbolic-link member whose target leaves the extraction root is
**skipped and named**, not fatal:

```text
warning[CPDL-W6001]: source: source `edk2`: member
  "edk2-master/EmulatorPkg/Unix/Host/X11IncludeHack": skipped: symbolic link
  target `/opt/X11/include` leaves the archive root
```

The confinement rule is unchanged. Never creating the link satisfies "nothing
is written outside the root" exactly, so refusing the archive protected nothing
further while denying every legitimate member. A link named explicitly in an
`extract { member … }` block is still a refusal, and an escaping *hard* link
still fails, because that would expose outside content rather than dangle.

This was reported against cix#584. As of 2026-10-30 the corpus carries
`ovmf`, `qemu`, `edk2-basetools`, `nasm` and `acpica`, and they validate —
`ovmf` fetches the edk2 tarball directly and `qemu` the qemu tarball that
vendors it.

### A relative `--staged` works

The build roots were composed straight from `--staged`, so a relative value
produced relative roots, and confinement refuses a root that is not absolute.
Every confined operation then failed with the message for an *attempted escape*,
naming a path that was inside the roots and present on disk:

```text
error[CPDL-E4005]: runtime: required file `${src}/…` resolves outside the
  confined build roots
```

From v0.1.108 the workspace is resolved once, before any phase runs, for the
library entry points as well as the CLI. Workspace spelling cannot reach the
artifact: the gate builds the same recipe through a relative and an absolute
workspace and requires byte-identical output.

### A plain-text source is no longer a malformed archive

`materialize` exists for configuration fragments, and a whole class of them was
refused. libarchive's mtree reader claims any text whose first line reads
`word key=value` — `contents`, `mode`, `size`, `uid`, `gid` are all mtree
keywords — and CBS escalated that guess to a hard `CPDL-E6001`. **JSON could not
be materialized at all.**

Fixed in v0.1.114. A format CBS cannot parse, in a format it does not support,
is an ordinary file; one it *can* parse but does not support stays a deliberate
refusal, so a real cpio is not materialized as an opaque blob.

Latent rather than live: no corpus source was a text config at the time.

## For cixd and other embedders

### The event stream can leave standard error

`cbs build --events human|jsonl` has always written to standard error, so a
parent that wanted an operator-readable log gave up the structured stream, and
one that wanted records put them on the same descriptor as every child's output.

From v0.1.107 two options name an independent destination:

```sh
cbs build RECIPE.cbs --arch ARCH --staged WS --output ART \
    --events human --events-fd 3     # prose on stderr, JSONL on fd 3
```

`--events-file FILE` is the same thing to a path. The destination always
carries JSONL, needs no `--events` of its own, and pairing it with
`--events jsonl` moves the stream rather than duplicating it. CBS duplicates the
descriptor, so closing its reporter never closes one the parent still owns, and
a descriptor that is closed or not open for writing is a usage refusal — exit 2,
`CBS-E1022` — *before* the build begins. `--report` aggregates the same events
either way.

This also settles a stale note in cix ADR-0275, which says CBS emits no phase
event and cites issue #131 as outstanding. `phase-begin` has in fact been
dispatched for some time; only the destination was missing.

### `check` and `validate` are the same command

Exact synonyms, both accepting `--json`. `--help` previously presented one as an
alias of the other and advertised `--json` on only one; the README said the
alias ran the other way. Each verb reports itself in the `cbs.diagnostic/v2`
envelope, which is their only observable difference.

## Diagnostics

Each of these was a correct refusal carrying a message that described something
else. The project's rule is that a failure names what failed.

| Was | Now | Release |
| --- | --- | --- |
| a non-directory parent reported `ELOOP`, indistinguishable from a confinement refusal | `ENOTDIR` for a collision, `ELOOP` kept for a symlinked parent | v0.1.110 |
| a failed `symlink` named its target | names the link path it could not create | v0.1.110 |
| `require tool … for` gave a generic "expected phase operation" | `CPDL-E2002` naming the rule | v0.1.112 |
| a `materialize` destination refusal named the source, and said the source was at fault | names the destination, with its own reason | v0.1.116 |

The first row matters most: a genuine escaping-symlink refusal and an ordinary
path collision produced the identical string, so an operator could not tell a
security refusal from a typo.

New codes, both appended rather than reused: `CPDL-W6001` (archive member
skipped) and `CPDL-E2002` (unexpected token, which §8.2 had allocated and
nothing emitted). The `E6xxx` and `W6xxx` ranges are now specified.

## Specification corrections

No behaviour change; the specification was describing less than the
implementation accepts.

- §2.4 claimed to list the complete keyword set and listed 70 of **109**,
  missing 39 including the phase name `install`. Since the sentence after it is
  normative — an unknown word is an identifier only where the grammar permits
  one — that list defines what is reserved, and it left `install`, `license`,
  `metadata`, `replaces`, `resources`, `tools` and `privileged` looking
  available for a recipe author's own use.
- §4.7's `require-operation` omitted both `for` lists and `require config`
  entirely, and its prose claimed `for` works on any require operation when
  `require tool … for` is a parse error.
- The CIXPKG v2 specification never mentioned the `m key value` manifest
  metadata line that CBS writes, while instructing readers to treat an unknown
  line type as corruption — so a second implementation built from it would have
  rejected CBS's own artifacts.

## Verification

`make -j1 test` is the gate: 41 test groups, run by hand, offline. Every release
above passed it before the commit, after the commit, and again from the tagged
checkout.

The broader check is the recipe corpus, which is not in this repository: **1969
CPDL definitions across 245 distinct packages, all validating** against
v0.1.116 on 2026-10-30, up from 1010 and 181 three weeks earlier with no shell
recipes remaining.

The real-package qualification — build zstd twice, require byte-identical
artifacts — is the one gate step that does not run by default; it needs
`CBS_UPSTREAM_CACHE` pointing at a digest-keyed source cache.

## Where to read more

Each issue has a record under [`docs/reviews/`](reviews/README.md) stating the
decision, its evidence, and the acceptance criteria met: #282–#296 cover this
run. Those records are history — they describe the implementation at their close
date. [`docs/repository-status.md`](repository-status.md) is the authority on
what this repository owns today, and the issue tracker on what is open.
