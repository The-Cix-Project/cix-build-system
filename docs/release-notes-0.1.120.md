# CBS v0.1.106 – v0.1.120, and the licence change

For Cix maintainers, recipe authors, and anyone embedding CBS. It covers the
licence decision of 2026-10-23 and the fifteen releases of 2026-10-09 and
2026-10-30, which closed nineteen tracker issues.

Read the four **Act on this** items if you read nothing else. Items 3 and 4
are the ones that change what cixd sees from a build it already runs today.

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

### 3. Every build gets a fingerprint, and every cixd build had none

`cbs build` recorded `build_fingerprint unavailable` for **every recipe that
declares no `tools`** — which is all 1969 definitions in the corpus; none
declares one. Meanwhile `cbs fingerprint` printed a key for the same recipe and
architecture, so the integration contract's promise that the two agree was
false for every real recipe.

Separately, the pipeline wrote the literal string `"callback"` into the
finalizer material slot and then tried to open it as a path. It always failed,
so **every build that ran a finalizer recorded no fingerprint at all** — which
is every build cixd runs, since cixd always supplies one.

Both are fixed in v0.1.119 and v0.1.120.

**What this means for you.** A build you run today returns a key where it
returned `unavailable`, and that key equals what `cbs fingerprint` precomputes,
so it is usable for a cache lookup before building. Keys also *change value*
for the few recipes that had one, because the removed material (below)
contributed to them. No published key is affected: `cbs_version` is part of the
material, so every key already changed on every release.

**What the key binds, precisely.** The normalized recipe digest, the policy
versions, the architecture, the approved command and library paths, the
declared input digests, and **a digest of every executable the recipe runs**,
each resolved through the approved command path. It does not hash the rest of
the search path.

The reason it did not work is worth stating, because it changes what the key
means. CBS walked the whole command and library path as material, using the
*staged-tree* collector — so it applied CIXPKG packaging rules to `/usr/bin`,
where a setuid binary, a hard link, or a symlink pointing out of the directory
is perfectly normal. One such entry refused the walk and cost the build its
entire fingerprint, with a generic reason that named nothing. On the host this
was measured, `/usr/bin` and `/bin` hold 389 symlinks, 18 setuid binaries and
11 hard-linked files, and `/lib` is itself a symlink to `usr/lib`.

The walk had never run against a real host; its only test drove small synthetic
directories. Making it succeed was tried and measured: **56 s per build** for
`/usr/bin` alone, ~4 GB and 21,900 files across the default paths. So it is
removed rather than repaired, because CBS already binds each command the recipe
invokes by digest, and `tools { … }` and `--tool-identity` bind a composed
toolchain deliberately. A second toolchain-binding mechanism beside one that
works is the kind of parallel this project does not keep.

**If you need the whole toolchain bound, pass `--tool-identity`** — that is what
the option is for, and the same complete set must go to `fingerprint` and
`build`. Two caveats stated plainly: `fingerprint` accepts no
`--prune-policy`, `--firmware-root` or `--finalize-command`, and all three
enter the key, so a build using one is not predictable from the verb; and a
finalizer is bound only as *having run*, not by its own digest, so two
different `--finalize-command` scripts produce the same key.

### 4. `CBS_ABI_VERSION` went 3 → 6; rebuild

Three releases changed the installed header. An embedder built against ABI 3
must be rebuilt, and the guard in `docs/library.md` is what catches it.

| Bump | Release | What changed in the installed header |
| --- | --- | --- |
| 3 → 4 | v0.1.117 | `CbsExecutionContext` gained the build-wide event counter |
| 4 → 5 | v0.1.118 | `CbsBuildEvent` gained `exit_status` and `signal` |
| 5 → 6 | v0.1.119 | `CbsBuildReport` gained `output_count` |

Upgrade the header, static archive and executable together from one tag, then
rerun the install contract test.

## New capability: several packages from one recipe

A split package — `cixctl` carved out of `cix`, or a `-dev` set — used to need
a second recipe pinning the same source and running the same build. That is two
sources of truth for one build action: they can drift, and nothing in either
artifact said they came from the same compile.

From v0.1.119 a recipe may declare `output` blocks (ADR-0038):

```cpdl
package "cix" {
    version "1.4.0"
    release 2
    format "cixpkg"
    requires { runtime { library "libarchive" } }

    output "cixctl" {
        files { "usr/bin/cixctl" }
        requires { runtime { library "libcurl" } }
        license "LGPL-3.0-or-later"
    }
    output "cix-tools" { files { glob "usr/bin/mk*" } }

    build { … }
}
```

```text
cbs build cix.cbs --arch x86_64 --staged WS --output-dir DIR
  -> cixctl-1.4.0-2-x86_64.cixpkg      usr/bin/cixctl
  -> cix-tools-1.4.0-2-x86_64.cixpkg   usr/bin/mkbootroot, mkinstalleriso
  -> cix-1.4.0-2-x86_64.cixpkg         usr/bin/cixd, and the directories
```

The rules that matter when you write one:

- The `package` declaration **is** an output — the main one — and receives
  every staged path no `output` claimed. So carving one binary out of a large
  package does not mean enumerating the rest.
- A path two outputs claim is **refused**, naming the path and both outputs,
  rather than resolved by declaration order. A package's contents must not
  depend on the order its outputs happen to appear in.
- Either every artifact is written or none is. A failure part way through
  unlinks what it wrote, so a repository never holds part of a set.
- `files` reuses the glob vocabulary `copy` and `require glob` already have.
  `files { glob "**" }` claims everything, including the directories, and the
  main package is then not emitted.
- An output may carry its own `requires`, `license` and `replaces`.

**CIXPKG v2 is unchanged.** Each output is an ordinary artifact that verifies
and extracts standalone, with its own identity and digest. All of them record
**the same `build_fingerprint`**, because there was one build action — that
shared value is what says these artifacts came from one build and cannot have
drifted from each other.

`cbs build --output-dir DIR` derives the canonical
`name-version-release-arch.cixpkg` per output. `--output FILE` is the
single-output form and is refused when a recipe declares more than one, since
one path cannot name several artifacts.

**The corpus is unaffected.** The grammar is additive and no existing recipe
declares an output; all 1969 still validate.

Approval remains the repository's decision. CBS's part is that the fingerprint
is in every artifact's metadata and the digests are in `explain` and the
events, so an approval naming the build action can be checked against each
artifact.

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

### Event sequence numbers are now monotonic — cix#388 can stop ignoring them

`cbs_emit_build_event` advanced the counter held in whichever context it was
handed, and a phase, a `cd` block and a `check` case each execute in a copy.
The copy's counter advanced for events nobody else saw and was never carried
back, so a `phase-end` carried a number lower than the commands inside its own
phase, and the next phase reissued numbers already used.

Measured on a three-phase, four-command fixture: **17 events, 9 distinct
sequence numbers**, with 3 through 8 each issued twice. The integration
contract's claim that they increase monotonically within a build was simply
false, which is why cix#388 ignores the field.

From v0.1.117 the counter belongs to the build: `CbsExecutionContext` carries a
pointer to the originating context's counter, the pipeline sets it once, and
every copy inherits the pointer rather than a value. Carrying it back from each
copy was rejected — there are three copy sites today and a missed write-back
reintroduces the defect silently. A shared pointer is correct by construction.

`sequence` is now dependable as an ordering key and a gap detector.

### `command-end` carries the command's own exit code or signal — cix#489

`status` is whether the command met its recipe's expectation, so a command
killed by a signal and one that exited 2 both reported `1` and nothing else.
"Which command failed, and how" is the first question asked of a build report,
and both values were already in hand at the wait site and dropped.

From v0.1.118 `CbsBuildEvent` carries:

- `exit_status` — the command's own code, or `-1` when it did not exit;
- `signal` — what killed it, or `0`.

The human reporter now says `FAILED (exit 1)` and `FAILED (signal 15)` rather
than a bare `FAILED`. A timeout therefore reports the signal that enforced it
instead of looking like an ordinary non-zero exit.

### The build report counts outputs

`CbsBuildReport` gains `output_count`. When it exceeds 1, `artifact` and
`artifact_digest` are left **empty** rather than naming one of N as though it
were all of them; `artifact_bytes` and the other totals accumulate, because a
total is one fact about the build. The `artifact-finalized` events enumerate
the artifacts, one per output.

`cbs explain --json` reports an `outputs` array **always**, main package first,
even for a single-output recipe — one shape to read rather than two. Each entry
carries the output's name, its `files` selectors and its own `requires`, in the
same serialization the top-level `requires` uses. An orchestrator therefore
knows what a recipe provides before building it.

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

`make -j1 test` is the gate: 44 test groups, run by hand, offline. Every release
above passed it before the commit, after the commit, and again from the tagged
checkout. The parser and validation suite is 71 cases, and they also pass under
TCC bounds instrumentation (`tcc -b`) as of v0.1.120.

The broader check is the recipe corpus, which is not in this repository: **1969
CPDL definitions across 245 distinct packages, all validating** against
v0.1.120 on 2026-10-30, up from 1010 and 181 three weeks earlier with no shell
recipes remaining. Re-measure rather than quoting that figure.

The real-package qualification — build zstd twice, require byte-identical
artifacts — is the one gate step that does not run by default; it needs
`CBS_UPSTREAM_CACHE` pointing at a digest-keyed source cache.

## Where to read more

Each issue has a record under [`docs/reviews/`](reviews/README.md) stating the
decision, its evidence, and the acceptance criteria met: #282–#300 cover this
run. [ADR-0038](adr/0038-multiple-package-outputs.md) carries the reasoning for
multiple outputs, including the alternatives rejected. Those records are
history — they describe the implementation at their close date.
[`docs/repository-status.md`](repository-status.md) is the authority on what
this repository owns today, and the issue tracker on what is open.
