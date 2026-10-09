# CBS — Cix Build System

CBS is a standalone C engine that lexes, parses, validates and executes **CPDL**
recipes (`.cbs` files) and emits verified **CIXPKG v2** artifacts. It is built
with TCC, links only libarchive/libzstd/libdl, and never hands recipe text to a
shell.

Current release is the first line of `VERSION` (0.1.109 at time of writing).
Language contract: CPDL 1.0. Artifact contract: CIXPKG v2.

## Scope: what lives here and what does not

This repo is **only** the build engine. `cixd` — the daemon that owns
containers, sandboxing, image transactions, repository publication, signing-key
lifecycle, package-graph resolution, and the authoritative recipe corpus — is a
**separate repository that is not present here**. Those capabilities appear here
only as callback seams (`src/api.c`, `src/sandbox.c`, `src/service.c`,
`src/signature.c`, each a few lines long). Never describe cixd-owned behavior as
a CBS feature. `docs/repository-status.md` is the authority on that boundary.

> **Ignore `/home/osakka/CLAUDE.md` while working in this repo.** It documents an
> unrelated agent-orchestration platform (`ccmf`/`aiwmd`, `ccmf_client`, worker
> registration, port 8085). Nothing in it applies here; do not run its commands.

### The real recipe corpus is next door

`docs/` describes the authoritative corpus as external, and it is — but it is
checked out on this machine at **`/home/osakka/cix-recipes`** (its own repo,
split from `cix#505` on 2026-09-21). It holds **993 `.cbs` recipes** under
`recipes/package/`, `recipes/image/`, `recipes/deployment/`, named
`<name>@<version>.<ext>`.

**All 993 validate against this engine** (checked 2026-10-09). That makes it the
fastest real-world regression check for any CPDL or validator change — far
broader than the single `recipes/zstd.cbs` fixture shipped here:

```sh
cd /home/osakka/cix-recipes && for r in $(find recipes -name '*.cbs'); do
  /home/osakka/cix-build-system/cbs validate "$r" >/dev/null || echo "FAIL $r"
done
```

No `cixd` checkout exists on this machine. `/home/osakka/cix-cache` is a
different project, not a CBS source cache.

## Commands

```sh
make                 # version-check + ./cbs + libcbs.a     (TCC only)
make -j1 test        # the full gate: 41 test groups, offline-clean
make clean
make install PREFIX=/usr/local   # cbs, libcbs.a, cbs/cbs.h, cbs.pc
```

`make test` is **fully offline**. `tests/upstream-smoke-test.sh` prints
`SKIP` and exits 0 when `CBS_UPSTREAM_CACHE` is unset — it does not fail and does
not download. To actually run the zstd qualification (build twice, require
byte-identical artifacts):

```sh
make upstream-test CBS_UPSTREAM_CACHE=/path/to/source-cache   # tarballs named by SHA-256
```

Other targets: `recipe-test` (every `recipes/*.cbs` validates), `version-check`
(refuses a VERSION/tag mismatch), `qualification-test` (= `test`).

Focused loops while developing:

```sh
./cbs check recipe.cbs                 # validate, no execution
./cbs explain recipe.cbs --json        # the plan + full metadata, no execution
./cbs doctor recipe.cbs --arch x86_64 --staged WS   # preflight, never mutates
./cbs fingerprint recipe.cbs --arch x86_64
./tests/cli-contract-test.sh ./cbs     # shell tests take the binary as $1
```

`./cbs --help` is the complete verb list; `./cbs --capabilities` emits the
machine-readable version/ABI/verb manifest. Every failable verb accepts
`--diagnostics=jsonl`.

## Layout

```
src/        the engine (~17k lines C11) and both headers
tests/      *.c focused tests, *.sh contract tests, fixtures/
recipes/    zstd.cbs — the one build-tested qualification recipe
docs/       spec/ (normative), adr/ (37 decisions), guides/, reviews/ (closed-ticket evidence)
cbs.cbs     CBS's own self-hosting recipe (pin necessarily lags one release)
```

`src/cbs.h` is private (lexer/parser/AST/execution internals).
`src/cbs_public.h` is the installed ABI, copied to `cbs/cbs.h` — treat it as a
versioned boundary (`CBS_API_VERSION`, `CBS_ABI_VERSION`,
`cbs_execution_context_size()`).

## Where things live

| File | Owns |
| --- | --- |
| `lexer.c` | bytes → tokens. UTF-8/CRLF normalization, string & block-string forms. No keyword table — every word is `CBS_TOKEN_WORD`. |
| `parser.c` | tokens → AST. Keywords are matched **contextually** via `is_word()`. `parse_package_item()` is the authoritative top-level declaration list. |
| `validate.c` | every statically decidable rule. Largest single rule set; emits most `CPDL-E3xxx`. |
| `ast.c` | node create/add/destroy, `cbs_allocate`/`cbs_duplicate`. |
| `plan.c` | AST → ordered `CbsBuildPlan`, `cbs_build_metadata`, phase events. |
| `runtime.c` | `cbs_execute_block` — the **single** block executor; jobs ceiling; stage-path policy. |
| `exec.c` | `run`: direct `execve`, argv/env construction, timeouts, limits, command-path resolution, forbidden-executable policy. |
| `fs.c` | the confined filesystem vocabulary, source edits/assertions, globs, `materialize`, `patch`, `links`. Largest engine file (2.3k lines). |
| `archive.c` | safe libarchive extraction (hostile entries, links, pax, modes). |
| `source.c` | named sources, mirrors, SHA-256 verify, cache-first prepare, digests. |
| `fetch.c` | the libcurl fetch service handed in as `CbsFetchService`. |
| `identity.c` | the one canonical `name-version-release-arch` identity and its derivations. |
| `manifest.c` | deterministic typed staged-tree manifest, canonical ordering. |
| `cixpkg.c` | CIXPKG v2 write/verify/extract/list/diff. |
| `package.c` | the build orchestration: `cbs_build_standalone*`, fingerprints, compress. |
| `prune.c` | post-phase prune policy (strip-debug, static/libtool archives). |
| `observe.c` | ELF `DT_NEEDED` observation. |
| `report.c` | `CbsBuildEvent` sinks (jsonl/human) and `CbsBuildReport`. |
| `diag.c` | the one diagnostic emitter: human form and the `cbs.diagnostic/v2` envelope. |
| `kconfig.c` | curated `CONFIG_*` y/m/n merge. Nothing else from kconfig text. |
| `workspace.c` | creates `src/ build/ dest/ cache/ tmp/` at 0700. |
| `main.c` | CLI parsing and dispatch per verb; owns the 0/2/3/4 exit statuses. Also holds the whole `revise` implementation (~650 lines) and the `CIXPKG-E*` codes. |
| `api.c` `sandbox.c` `service.c` `signature.c` | the cixd seams. Deliberately trivial. |

## Pipeline

```
recipe → cbs_lex → cbs_parse → cbs_validate → cbs_build_plan
       → cbs_prepare_sources (fetch, verify SHA-256, extract)
       → cbs_execute_plan → cbs_execute_block → run / filesystem ops
       → finalize callback → prune policy
       → cbs_manifest_collect/_write → cbs_cixpkg_write_tree → verify
```

Workspace roots are `WS/{src,build,dest,cache,tmp}`, created at mode 0700.

The complete interpolation set lives in `exec.c:context_value` (line 205, the
name table) behind `cbs_resolve_value` (line 295) — read it rather than
guessing: `${name}`, `${version}`,
`${release}`, `${arch}`, `${triplet}`, `${src}`, `${build}`, `${dest}`,
`${case.dir}`, `${firmware}`, `${jobs}`, `${source.NAME}`, `${input.NAME}`,
`${stdout.NAME}`, `${stderr.NAME}`, `${glob.NAME}`, plus the parse-time
`${each.NAME}`. All are immutable; CPDL has no user variables.

## CPDL facts that are easy to get wrong

- **Phases begin in `$build`**, not `$src` — out-of-tree build is the default.
  A recipe that must work in the extracted tree writes `cd "${src}/NAME" { … }`.
  Extracted sources land at `${src}/<source-name>/…`.
- **`check` runs after `install`**, regardless of declaration order, so it can
  inspect the staged tree (`cbs_execute_plan` does two passes).
- Phases are declared in the fixed order `prepare configure build check install`,
  each at most once. `CBS_MAX_PHASES` is 5.
- `format "cixpkg"` and `release` are both **mandatory** — neither has a default
  (ADR-0030 records that the release default was never implemented). An
  `architecture` declaration in a recipe is a validation error (`CPDL-E3006`,
  "architecture is supplied by CBS in CPDL 1.0"); the target comes from `--arch`.
- `requires` item keywords are an **open vocabulary** carried through verbatim as
  the dependency `kind`. CBS does not solve dependencies.
- Exactly one `main` source. One checksum, one or more ordered mirror URLs.
- The child environment is **constructed by CBS**, never inherited. `exec.c:732`
  sets `SOURCE_DATE_EPOCH=0` in every child. A recipe `PATH` override is
  rejected — the composer supplies `--command-path` (default `/usr/bin:/bin`)
  and `--library-path`. `CBS_LOG_DIR` is the only environment variable the
  engine itself reads (opt-in per-command logs, mode 0600).
- No shell, pipes, redirection, variables, conditionals, arithmetic, regex, or
  runtime loops. `each` expands **at parse time**; `${stdout.NAME}` is an
  immutable binding. Unknown constructs must fail parse or validation — never be
  ignored or forwarded.

Exit statuses (ADR-0008, asserted by `tests/cli-contract-test.sh`):
`0` success, `2` CLI usage, `3` recipe/source/build failure, `4` artifact
verification or extraction failure. Nothing else — 5/6/70 were drafted and never
implemented.

## Code conventions

- **C11, TCC only**, `-Wall -Wextra -Werror -pedantic`. No GCC extensions. A
  warning breaks the build.
- `.clang-format`: LLVM base, 4-space indent, 80 columns, `char *name`, attached
  braces.
- Functions return **1 on success, 0 on failure**. Diagnostics are emitted at the
  point of failure, not propagated as codes. `main` returns the exit statuses
  above.
- `cbs_allocate`/`cbs_reallocate` abort on exhaustion — callers do not check for
  NULL from them. Plain `malloc` callers must.
- Declare locals at the top of the block; a short `/* … */` sentence above each
  function and struct field.
- Comments explain *why a rule exists*, matching the surrounding register (see
  `tests/temp.h`, `plan.c:cbs_execute_plan`). Don't add narration.
- Fail closed. A rejected event, a failed finalizer, or an unverifiable digest
  aborts the build and writes no artifact.
- Never widen `src/cbs_public.h` casually. New capability goes on a new
  `…_with_*` entry point (see the `cbs_build_standalone_*` chain) so existing
  callers keep their signature; bump `CBS_ABI_VERSION` when the layout changes.

## Adding a language construct

Touch the chain in order, or validation and execution drift apart:

1. `src/parser.c` — a `parse_*` function plus a branch in `parse_package_item()`
   or `parse_operation()`; add the `CbsNodeKind` in `src/cbs.h`.
2. `src/validate.c` — every statically decidable rule. Validation must not
   touch the filesystem, network, or spawn anything.
3. `src/fs.c` / `src/exec.c` / `src/runtime.c` — execution.
4. `src/main.c` — report it in `explain --json` if embedders need it.
5. `tests/fixtures/valid/` **and** `tests/fixtures/invalid/` — a rejection
   fixture may carry a sibling `.expect` file holding the required diagnostic
   text (16 of the 57 invalid fixtures do).
6. A focused test, wired into the `test:` target in the `Makefile` with its exact
   object list, run, then `rm -f`.
7. `docs/spec/cpdl-1.0.md` (§2.4 keyword set, §3.1 grammar, §8.2 codes) and
   `docs/cpdl-test-coverage.md`. An architectural change needs a new ADR first —
   ADRs are never rewritten, only superseded (ADR-0029).

## AST field conventions

`CbsNode` is a flat struct shared by every node kind, so a field's meaning
depends on the kind. Two conventions matter:

- `flag` and `second_flag` are the **token kinds** of `value` and
  `second_value` (`CBS_TOKEN_STRING`, `CBS_TOKEN_BLOCK_STRING`, …), used so
  interpolation and validation can treat a block string differently.
- `allow_failure`, `selector_glob`, `insert_before` and `stderr_stream` are
  genuine booleans with one meaning each.

Do not overload `flag`/`second_flag` with a boolean. Until v0.1.109
`second_flag == 1` also meant `allow_failure`, which silently converted every
failing `write` into a success, because a write stores its text's token kind
there (issue #285). `src/cbs.h` is repository-private and not installed, so
adding a field is free — do that instead.

When adding a field, also extend `clone_node()` in `src/parser.c`; `each`
expansion copies nodes through it, so a field it misses is silently lost in
expanded bodies.

## Adding a CLI option

`build`'s options are threaded by hand, so a new one touches seven places in
order: the `CbsBuildOptions` struct, the `--name=` prefix branch, the
space-form `strcmp` list, **an assignment branch** in the long `else if` chain,
the `build_package` signature, its call site, and the help string.

The trap is that `parse_build_options` ends with a bare
`else options->events = value;` catch-all. An option added to the space-form
list without its own assignment branch is silently parsed as `--events`
instead. Always add the assignment branch, and test the flag end to end.

CLI behaviour is covered by the `tests/*.sh` contract tests
(`observability-test.sh` for events, `cli-contract-test.sh` for verbs and exit
statuses), not by the C tests. Document a new option in `docs/user-manual.md`,
in `docs/integration-contract.md` if an embedder would use it, and in the
ADR-0008 implementation note — the command surface is ADR-0008's, not the
CPDL spec's.

**Diagnostic codes are append-only.** `CPDL-` ranges: `E1xxx` lex, `E2xxx`
parse, `E3xxx` validation, `E4xxx` runtime/operation, `E5xxx` source
preparation, `E9xxx` internal invariant, `W3xxx` warnings, `Nxxxx` notes.
Artifact failures use a separate `CIXPKG-` namespace, all emitted from
`main.c`: `E4001` corruption, `E4002` destination preparation, `E4003`
destination exists, `E4004` missing destination parent. Never reuse or
repurpose a code. Every diagnostic goes through `diag.c` and starts with
`PATH:LINE:COLUMN: SEVERITY[CODE]: CATEGORY: MESSAGE`, one sentence, no trailing
period.

## Tests

Every C test includes `tests/temp.h` and writes under its own `mkdtemp` root
honouring `TMPDIR`, removing it before exit. Shell tests take the binary as `$1`,
use `set -eu`, and clean up with a `trap`. Each test prints one
`<name>: PASS (<what was actually asserted>)` line — the parenthetical is the
convention, not decoration. A new surface belongs in the
`docs/cpdl-test-coverage.md` matrix.

Expected diagnostic text appearing in `make test` output is normal: several
tests assert failure paths.

## Documentation authority

1. `src/` — the implementation is the final authority.
2. `docs/spec/cpdl-1.0.md`, `docs/spec/cixpkg-1.0.md` — normative, now closely in
   sync with the code.
3. `docs/repository-status.md` — authority on the CBS/cixd boundary.
4. `docs/adr/` — decisions and rationale. ADR-0008 fixes the command surface.
5. `docs/user-manual.md` (users), `docs/standalone-runbook.md` (operators),
   `docs/integration-contract.md` + `docs/library.md` (embedders),
   `docs/guides/ci-and-release.md` (maintainers).
6. `docs/reviews/issue-NNNN-validation.md` — evidence at the time each ticket
   closed. **Historical**: they over-claim scope and describe past state. Absence
   of a record does not mean a ticket is open.

## Release convention

**`docs/guides/ci-and-release.md` is canonical.** Follow its checklist, not
whatever the last few commits happened to do:

1. Update implementation, tests, guides, and the current-release references.
2. `git diff --check`, then `make -o version-check test`. The full
   `make -j1 test` runs after the commit and again from the new tag (step 7) —
   `version-check` refuses a bumped `VERSION` while `HEAD` is still the
   previous tag, so a pre-commit full gate cannot pass. The guide says this
   since `bc277db`.
3. Confirm the upstream qualification either passed or was explicitly skipped
   because `CBS_UPSTREAM_CACHE` is unset.
4. Commit with a focused message: a lowercase imperative subject, then a body
   explaining **why**, ending `Resolves #N.` for a ticket (see `d31f91a`,
   `87d4ec0`). Subject-only, no body, is for changes with nothing to explain
   (e.g. `Allow directory checksum list URLs`).
5. Annotated tag, never lightweight: `git tag -a v0.1.N -m "CBS v0.1.N"`.
6. Push the branch and tag together: `git push origin main v0.1.N`.
7. Re-run `make -j1 test` from the tagged checkout.
8. Update the issue tracker with the commit, tag, test result, and any
   intentional qualification skip.

Step 1's reference sweep is **required policy**, not churn. Every release bumps
`VERSION` and rewrites the release string in all eight files that name it:
`README.md`, `docs/README.md`, `docs/repository-status.md`, `docs/roadmap.md`,
`docs/user-manual.md`, `docs/integration-blockers.md`,
`docs/guides/ci-and-release.md`, `docs/adr/0008-cbs-command-surface.md`.
Confirm with `grep -rn "0\.1\.N" --include="*.md" . | grep -v docs/reviews/`.
Verify the result with `make version-check` (it refuses a VERSION/tag mismatch),
`make cbs`, and `./cbs --version`.

Note the recent tags deviate from the canonical flow — v0.1.101–105 fold the
`VERSION` bump into the feature commit and use the tag message
`Release CBS 0.1.N`. v0.1.100 used `CBS v0.1.100`, per the guide. Follow the
guide.

Moving the `cbs.cbs` self-hosting pin is a **separate, later** commit: fetch
`https://git.home.arpa/itdlabs/cix-build-system/archive/v0.1.N.tar.gz` (private
Gitea, needs `curl -sk -H "Authorization: token …"`; Gitea archives are
byte-stable), sha256 it, update `version`/`url`/`sha256`, then
`./cbs validate cbs.cbs` and `sh tests/recipe-metadata-test.sh cbs.cbs`. The pin
necessarily lags one release, because the tagged archive contains the recipe.

There is no CI configuration in this repository; the gate is run locally.

## Backlog

Tickets live in a private Gitea at `https://git.home.arpa/itdlabs/cix-build-system`
(use `curl -sk`; the cert is untrusted and anonymous requests get 404, never
401). The per-ticket loop: fix + tests + `docs/reviews/issue-NNNN-validation.md`
using the real ticket title + the index entry in `docs/README.md` + a commit
saying `Resolves #N` + push + a Gitea comment with before/after evidence + close
via `PATCH state=closed`.

## Residual spec drift (verified 2026-10-09)

Mostly closed since the CPDL 1.0 promotion. What remains:

- `docs/spec/cpdl-1.0.md` §2.4's keyword set omits twelve keywords the parser
  accepts: `license`, `metadata`, `replaces`, `resources`, `tools`,
  `privileged`, `truncate`, `links`, `patch`, `parents`, `leaf`, `same_as`.
  §3.1's grammar does list the declarations, so §2.4 is the stale part.
- `docs/spec/cixpkg-1.0.md`'s manifest section lists only `f`/`d`/`l` entries
  and never mentions the `m license <spdx>` metadata line that
  `src/manifest.c:251` writes. The line itself *is* specified, but in
  `cpdl-1.0.md` §3.1 instead.

Two items are **already filed** — check the tracker before re-investigating:

- **#286** (decision): the parser accepts `allow_failure` on `move`, CPDL 1.0
  §4.4 grants it only to `copy` and `remove`. Behaviour preserved pending a
  ruling on which is authoritative.
- **#287** (bug): `safe_parents()` reports `ELOOP` for a parent that is merely
  not a directory, so a confinement refusal and an ordinary path collision
  produce the identical message; and a failed `symlink` names its target rather
  than the link path it could not create.

Previously-noted drift that is now **fixed** — don't re-report it: a relative
`--staged` works (#284, v0.1.108) and artifacts are byte-identical either way;
every `write` failure now propagates (#285, v0.1.109); the `E6xxx` and `W6xxx`
ranges are specified (#283, v0.1.106); the
duplicate ADR-0012 is resolved (payload index moved to
ADR-0037); `make test` no longer needs network; `src/exec.c` no longer appends
`CC=` to `make` argv; `license`/`metadata`/`${firmware}` are specified; the
CIXPKG header layout (identity at bytes 160–223, 64-byte max, flags at 224)
matches `src/cixpkg.c` exactly; ADR-0030 already records that the release
default was never implemented.
