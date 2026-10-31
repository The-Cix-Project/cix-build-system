# CBS v0.1.106 – v0.1.128, and the licence change

For Cix maintainers, recipe authors, and anyone embedding CBS. It covers the
licence decision of 2026-10-23 and the twenty-three releases of 2026-10-09 to
2026-10-31, which closed twenty-nine tracker issues.

Read the six **Act on this** items if you read nothing else. Items 3 to 6 are
the ones that change what cixd sees from a build it already runs today.

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
declares no `tools`** — which is every definition in the corpus, 2022 of
them as of 2026-10-31; none
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

### 4. A finalizer is now bound by what it is, so your keys change

Until v0.1.123 the fingerprint bound a finalizer only as *having run*: the
pipeline recorded a marker, so **two different `--finalize-command` scripts
produced the same key**. A composer that changed its finalizer got a cache hit
on an artifact the previous one built.

The deciding fact came from cixd's own source: it passes
`--finalize-command /build/finalize.sh` on every build, the path never changes,
and the content changes release to release. So CBS now digests the command it
resolved, and the key moves with content rather than with where the finalizer
sits. The digest is recorded in the artifact as `finalizer`, so a stored
artifact says what finalized it.

`cbs fingerprint` accepts `--finalize-command` too, resolved and digested the
same way, so a finalizer build's key is predictable before the build — which is
what the key is for.

From v0.1.124 the verb also takes `--prune-policy` and `--firmware-root`, the
last two inputs that entered the key and could not be given to it (#305). The
prune policy is **loaded** rather than named, because the key binds the three
rules it resolves to and not the file. So every input in the key can be given
to the verb, and the documentation no longer carries a list of exceptions to
the parity claim.

**For a library embedder:** pass the identity to
`cbs_build_standalone_with_finalizer()` or
`cbs_build_standalone_outputs_with_finalizer()`. An absolute path is digested;
any other string is bound verbatim, which is how an in-process callback
declares itself. A callback with **no** identity now gets no key at all —
`build_fingerprint unavailable`, reason `finalizer-unidentified` — because
binding a marker would leave a material input out of a key that looks
complete. A `--finalize-command` caller is always identifiable and so is
unaffected.

### 5. `CBS_ABI_VERSION` went 3 → 6; rebuild

Three releases changed the installed header. An embedder built against ABI 3
must be rebuilt, and the guard in `docs/library.md` is what catches it.

| Bump | Release | What changed in the installed header |
| --- | --- | --- |
| 3 → 4 | v0.1.117 | `CbsExecutionContext` gained the build-wide event counter |
| 4 → 5 | v0.1.118 | `CbsBuildEvent` gained `exit_status` and `signal` |
| 5 → 6 | v0.1.119 | `CbsBuildReport` gained `output_count` |

Upgrade the header, static archive and executable together from one tag, then
rerun the install contract test.

### 6. A recipe command no longer inherits CBS's descriptors

Reported by the cix maintainer against v0.1.120, which is also how it was
caught: CBS's own `make test` failed when CBS was built by CBS.

CBS builds the child's argv and environment from scratch and rejects a recipe
`PATH` override, and this note has always said the child environment is
constructed and never inherited. The **descriptor table** did not follow that
rule. A recipe command received, all open and writable:

```text
3 -> pipe:[…]                 the same pipe as CBS's stdout
4 -> /build/cbs-events.jsonl  the --events-file the composer passed
5 -> /tmp/#2 (deleted)        the same file as CBS's stderr
6 -> pipe:[…]
```

With `--events-fd N` the child inherited the composer's own event channel at
the number the composer chose.

**Scope, stated precisely.** The descriptor was present and writable, so a
recipe command could open `/proc/self/fd/4`. An attempt to forge or corrupt an
event through it did **not** succeed — CBS writes the stream through its own
`FILE*` and that offset overwrites what a child appends, and shells are a
forbidden executable, so `>&4` is not reachable from a recipe. Treat it as a
contract violation and a hardening gap, not a demonstrated forgery.

It also had a practical consequence beyond hygiene: a nested `cbs` inherited
whatever the outer one held, so no descriptor number could be assumed free.
That is what broke the self-hosted `make test` — an assertion that
`--events-fd 9` must be refused is only true where fd 9 is actually closed.

From v0.1.121 the child's table is constructed like its argv: after the `dup2`
calls and before `execve`, every descriptor above stderr is closed, enumerated
from `/proc/self/fd` (a soft `RLIMIT_NOFILE` of 2^20 is ordinary, and a million
`close()` calls per command is not). `tests/descriptor-hygiene-test.sh` is new
and in the gate.

From v0.1.123 standard input is constructed too: a recipe command gets
`/dev/null`. The reason is measured, not tidiness — in a cixd build container
the inherited fd 0 is `/dev/console`, so a command that reads standard input
**blocks** until the daemon's stall watchdog kills the build, giving a timeout
that points away from its cause. `/dev/null` makes it an immediate EOF and a
diagnosable failure. Redirected rather than closed, because a closed descriptor
0 is taken by the child's first `open()`. No corpus recipe reads standard
input.

The same self-hosted build then found the other direction, fixed in v0.1.122:
CBS validated `--events-fd N` against **its own** descriptor table rather than
the caller's. The stderr reporter's `dup` ran first and took the lowest free
number, so where the parent left 3..8 occupied it took 9 — and `--events-fd 9`
was checked against CBS's own dup of stderr, passed, and the event stream was
written there. Measured: refused with 3..7 occupied, accepted with 3..8.

A composer passing a stale descriptor number that collided with one of CBS's
own therefore had its events written into that descriptor instead of being
refused. The check now runs before CBS opens anything, which is what the
comment above it always claimed.

## New capability: several packages from one recipe

A split package — `cixctl` carved out of `cix`, or a `-dev` set — used to need
a second recipe pinning the same source and running the same build. That is two
sources of truth for one build action: they can drift, and nothing in either
artifact said they came from the same compile.

From v0.1.119 a recipe may declare `output` blocks (ADR-0038):

```cbs
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

**The corpus was unaffected on arrival** — the grammar is additive, so every
recipe stayed valid without edits. It is now in use: as of 2026-10-31
`cix@0.2.57-639`, `-640` and `-641` declare `output "cixctl"`, splitting the
client out of the `cix` package, which is the cix#378 case ADR-0038 was written
for. One package uses it so far. All 2022 definitions validate.

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

**A validation error was emitted under the wrong prefix (v0.1.126, #307).**
Thirteen errors in the `upstream` validation were emitted as `CBS-E3002`,
`CBS-E3004` and `CBS-E3006` — a validation code in the namespace reserved for
the command's own diagnostics, defined in no document — instead of the
documented `CPDL-E3002`, `-E3004` and `-E3006`. They arrived in one commit on
2026-10-02 and went unnoticed for four weeks because nothing listed the codes.
They are now `CPDL-E3xxx`. This is a normalisation, not a repurposing: each site
already matched the documented meaning of the number it carried. If you matched
the `CBS-E3` spelling literally, match `CPDL-E3` instead; cixd was asked and
matches neither prefix.

**`links` on a file that is not an ELF object printed an unrelated cause
(v0.1.128, #310).** The assertion correctly failed, but its diagnostic said
`errno=17 (File exists)` for a shell script, and `errno=2 (No such file or
directory)` on another run, because the observer returned without setting
`errno` and the caller printed whatever an earlier call had left. It now reads
`errno=8 (Exec format error)`. The same assertion had no test for any of its
failure paths, only one positive case; a missing needed library, a present
forbidden one, an absent forbidden one and a non-ELF file are now covered.

**The command's diagnostic codes are now written down.** Twenty-two of the
twenty-four `CBS-E1xxx` codes appeared in no document, so "codes are
append-only" referred to a registry nobody could read. They are a table in the
user manual (section 8.1), together with `CBS-E0000` (the emitter's fallback)
and `CBS-E4000` (a runtime failure relayed under `--diagnostics=jsonl`), and the
documentation gate now fails if the source can emit a code that no document
lists.

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

`make -j1 test` is the gate: 49 test groups, run by hand, offline. Every release
above passed it before the commit, after the commit, and again from the tagged
checkout. The parser and validation suite is 74 cases, and they also pass under
TCC bounds instrumentation (`tcc -b`), re-run at v0.1.128.

The broader check is the recipe corpus, which is not in this repository:
**2022 CPDL definitions across 248 distinct packages, all validating** against
v0.1.128 at `origin/main` `2a86e7f` on 2026-10-31, up from 1010 and 181 three
weeks earlier with no shell recipes remaining. That figure moved six times in
one day — 1969, 1983, 2012, 2013, 2016, 2022 — which is why no living document
carries it any more and it is quoted only here, in a dated record.

The real-package qualification — build zstd twice, require byte-identical
artifacts — is the one gate step that does not run by default; it needs
`CBS_UPSTREAM_CACHE` pointing at a digest-keyed source cache.

## A note on trusting this document

From v0.1.125 the claims in CBS's documentation that *can* be checked are
checked by the gate: that every file the release guide names contains the
release string, that the ADR, validation-record and test-group counts match the
filesystem, that every relative link resolves, that ADR-0008's note names every
verb `cbs --help` exposes, and that every long option appears in the user
manual or the standalone runbook (#306).

This was not pedantry. Three times in two days a release sweep made the
documentation wrong and nothing noticed: it broke the link to this note on
three consecutive releases, it rewrote the version inside a measurement
sentence so the claim named a version it had never been run at, and it silently
matched nothing where a count's wording had drifted. The sweep now reports its
match count per file, and a sentence reporting a measurement states the version
it was measured at, updated only by re-measuring.

v0.1.126 is the pass the gate cannot do, the prose (#308): every guide and
most reference documents were read against the code. It found a recipe-author
manual that never explained how to write an `output` block or what a command
receives, an embedding guide that omitted multi-output, a determinism contract
claiming the host's compilers never reach a build, a roadmap still listing
declined CI as pending, and a native-helper review that ADR-0004 requires at
every release and that had not run for thirteen. The last is done and logged.
Where a claim concerned cixd rather than CBS, the cix maintainer was asked, and
the blocker register now says only what CBS guarantees and points at their
repository.

v0.1.127 closes the cheapest gap that was still open (#309): **the examples are
run.** A complete recipe in the manual or the specification is fenced
` ```cbs check ` and the gate validates it. The first run found three of the four
complete examples invalid, the manual's first recipe and the specification's own
"complete shape" among them, all for one missing line: `format "cixpkg"` is
mandatory and has no default, and each example omitted it. So the first thing a
new user pasted from the manual was rejected with `CPDL-E3001`, and had been
since it was written on 2026-09-10. Fixed, and the "Important rules" beside that
recipe now say `format` is required. The check is opt-in because most `cbs`
blocks are deliberate fragments, and it fails closed: no tagged block, an
unknown tag, or the `cpdl` spelling of the fence is an error. The gate also now
fails if the native-helper review log's newest entry does not name the release,
since that review went unlogged for thirteen releases when nothing looked.

v0.1.128 closes the gap that was listed last: **absences** (#310). The
specification reserves a set of keywords, so the gate now requires each to be
shown as code in the manual. The first run found five implemented constructs the
manual never mentioned: `links` with `needs` and `forbids` (used by 14 corpus
recipes), `patch`, `truncate`, the `nonempty` property, and the `checksums` and
`signature` forms of `upstream verify`. They are documented, and the
specification gained the missing prose for `links`. Written as a rule, "the word
appears" was wrong three ways before it was right: English words from the
sentence above the list, a keyword that matched ordinary prose ("links against
libarchive"), and a quoted error message in backticks. It now counts only code,
and the residual limit is recorded in the test.

Exact corpus counts were also removed from the living documents. Dating each
copy had not stopped them going stale; they now appear only in dated records.

What this does **not** do is read prose. "cixd always supplies a finalizer" or
"no corpus recipe reads standard input" can only be re-measured by a person,
and the corpus figures above went stale inside a day. Treat a statement here as
dated, and re-measure anything you are about to depend on.

## Where to read more

Each issue has a record under [`docs/reviews/`](reviews/README.md) stating the
decision, its evidence, and the acceptance criteria met: #282–#310 cover this
run. [ADR-0038](adr/0038-multiple-package-outputs.md) carries the reasoning for
multiple outputs, including the alternatives rejected. Those records are
history — they describe the implementation at their close date.
[`docs/repository-status.md`](repository-status.md) is the authority on what
this repository owns today, and the issue tracker on what is open.
