# Issue #300 validation: every corpus recipe gets a build fingerprint

## What was broken

`cbs build` recorded `build_fingerprint unavailable` for every recipe that
declares no `tools` and is given no `--tool-identity` — all **1969** recipes in
the corpus; `grep -lE '^\s*tools\s*\{'` matches none of them. The recorded
reason was the generic `fingerprint-material-unavailable`, so nothing said why.

`cbs fingerprint` on the same recipe and arch printed a key, because the verb
passed a NULL material context and so skipped the whole material block. The
integration contract promises the two agree. For any corpus recipe they could
not, which also made ADR-0038's "one build action, one fingerprint" vacuous:
every output of a multi-output build shared the value `unavailable`.

## Cause

`fingerprint_append_search_paths` walked the command and library path as
fingerprint material through `cbs_manifest_collect` — the **staged-tree**
collector, which enforces CIXPKG packaging rules. A host search path fails
those rules:

```
/usr/bin: corepack -> ../lib/node_modules/corepack/dist/corepack.js escapes
the staged root; CIXPKG symlink targets must be relative and contained
```

Measured on this host, `/usr/bin` and `/bin` hold 389 symlinks, 18 setuid
binaries and 11 hard-linked files, and `/lib` is itself `-> usr/lib`. Each is a
legitimate host fact; each refused the walk, and one refusal failed the whole
fingerprint. `tests/package-test.c` only exercised the walk against small
synthetic roots it created, which is why the gate never saw it. **The walk had
never run against a real host.**

Making it succeed is not the fix. The default paths are 21,900 files and ~4 GB
here; digesting `/usr/bin` alone measured **56 s per build**, and the whole
default set minutes.

## Why the walk went rather than got fixed

`fingerprint_append_tools` already resolves every command the recipe `run`s
through the approved command path and binds that executable by digest. Declared
`tools` bind each alias's resolved target; `--tool-identity` binds a composed
toolchain by verified manifest digest without touching the host at all.

So the walk was a second toolchain-binding mechanism, unbounded in cost and
broken in practice, beside one that is precise, cheap and works. That is the
parallel the owner's principle forbids, and the narrower mechanism is the one
source of truth. The walk is removed; nothing replaces it.

The consequence is stated plainly in the user manual and the integration
contract: the key binds the commands a recipe invokes, not every file on the
path. A composer that needs the whole toolchain bound supplies
`--tool-identity`, which is what that option is for.

## Second instance, same release

The pipeline set `finalize_path` to the literal `"callback"` when a finalizer
existed, and the material loop `stat()`ed it as a path. It always failed, so
**every build that ran a finalizer recorded no fingerprint** — including every
cixd build, which always supplies one. A finalize path that is not a path is
now bound as an opaque value: "a finalizer ran" is the material fact, and
refusing to measure it lost the entire key.

## Kept from the investigation

- `cbs_manifest_collect_material()` — the same walker with the packaging rules
  lifted, used for `firmware_root` and the tool directory, which are host roots
  where a setuid bit or an escaping symlink is equally legitimate. One walker
  and one entry shape, a parameter rather than a second implementation. A
  material file CBS cannot read is recorded with no digest instead of aborting,
  so the observation still binds its presence, mode and size.
- A material root that cannot be observed now names itself:
  `<name> unobservable: <path>: <strerror>`, surfacing as
  `build_fingerprint_reason material-unreadable:…` instead of the generic
  string that hid this for so long.
- An unsupported file type in a material root is still a hard refusal. Nothing
  on this host's search paths is one, and silently skipping an entry is the
  failure class #285 and #294 were about.

## Acceptance criteria

`tests/cli-contract-test.sh`: a recipe that declares no tools and is given no
identities gets a 64-hex key from `cbs fingerprint`, and the artifact's
`build_fingerprint` is that same key. Both existing parity cases
(`--tool-identity`, declared `tools`) still hold, and the dynamic-run fixture
still records `unavailable` with `dynamic-run:./configure`.

`tests/multi-output-test.sh`: the fingerprint every output shares is a real
digest, and it equals what `cbs fingerprint` precomputes — so ADR-0038 §3 now
asserts something.

`tests/package-test.c`: the command and library paths are declared policy and
perturb the key; **editing a file on the search path does not**; replacing the
resolved command does; a command path that resolves none of the recipe's
commands still refuses a key. The two assertions that encoded the removed
design — a header under `command_path` perturbing the key, and creating an
absent search root perturbing it — are gone with it, not worked around.

Verified by stashing only `src/` and running the new assertions against the
pre-fix engine, where `cli-contract-test.sh` and `multi-output-test.sh` both
fail.

Full gate green; all 1969 corpus recipes still validate. A plain
`cbs build` is 0.016 s where the attempted fix measured 56 s.

## Not changed

No ABI change: `cbs_manifest_collect_material` is declared in the private
`src/cbs.h`, because material is never packaged and no embedder needs it.
`CBS_ABI_VERSION` stays 6.

Keys change value for recipes that previously got one, since the removed walk
contributed material. No published key is affected: `cbs_version` is in the
material, so every key already changes every release.
