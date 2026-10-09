# Issue #284 validation: `require` reports a confined path as escaping the build roots when `--staged` is relative

## Cause

`src/package.c` composed the build roots straight from the caller's workspace
string:

```c
snprintf(src, sizeof(src), "%s/src", workspace);
```

A relative `--staged ws` therefore produced the relative roots `ws/src`,
`ws/build`, `ws/dest`. Confinement resolves a recipe path with
`normalize_absolute()`, which refuses anything not beginning with `/`
(`EINVAL`), and `safe_root()` independently requires `root[0] == '/'`
(`EPERM`). So every confined operation failed, and the failure surfaced as the
message for an attempted escape — naming a path that was inside the roots and
present on disk.

The reported `CPDL-E4005` from `require` was one symptom. `mkdir` failed the
same way with `CPDL-E4004 ... errno=22 (Invalid argument)`.

## Decision

Resolve the workspace once, in the pipeline, before `cbs_workspace_prepare()`
and before the roots are composed. A relative path is joined to the current
directory; an absolute one is used unchanged.

Resolving was chosen over refusing a relative `--staged`. Nothing in the CLI,
`cbs --help`, or the user manual ever said the workspace had to be absolute,
and everything except confinement already worked with a relative one, so a
refusal would have turned a fixable inconsistency into a documented
restriction. Doing it at the pipeline chokepoint rather than in `main.c` means
library embedders get the same resolution as the CLI.

Symbolic links are deliberately **not** resolved. `safe_root()` already refuses
a symlinked root, so `realpath()` would only change which message such a caller
sees, and it would silently rewrite a workspace the caller named deliberately.

## Evidence

A recipe asserting `require file "${src}/s/pkg-1.0/file.txt" { exists }` against
an extracted source, at `bc277db` and with this change:

| `--staged` | before | after |
| --- | --- | --- |
| `ws` (relative) | `CPDL-E4005` ... resolves outside the confined build roots | builds |
| `$PWD/ws` (absolute) | builds | builds |

The file was on disk at `ws/src/s/pkg-1.0/file.txt` in the same run that
rejected it, which is what made the message actively misleading rather than
merely unhelpful.

Checked for collateral: `prune` builds its policy context from the same root
and now applies rules correctly under a relative workspace (`prune-remove`,
`drop-static-archives`); `cbs package ROOT` and `cbs doctor --staged` already
accepted relative paths and are unchanged.

## Acceptance criteria

1. **A relative workspace builds** — `tests/workspace-diagnostic-test.sh` runs
   `standalone-smoke.cbs` from the workspace's parent with `--staged ws`. That
   fixture uses `mkdir`, `write` and `require file`, all of which resolve
   against the roots, and the test asserts the staged file exists afterwards,
   so it proves confinement accepted the workspace rather than that the build
   merely did not crash. The artifact is verified.
2. **No workspace path reaches the artifact** — the same recipe is built again
   through an absolute workspace and the two artifacts are compared with `cmp`.
   They are byte-identical.
3. The existing missing-workspace and unsupported-format rejections are
   unchanged, and still assert their own diagnostics.

The new assertions were run against the previous binary and fail there.

## Found separately, not fixed here

While tracing this, `write` turned out to swallow **every** failure and report
success, independent of this issue: `cbs_execute_filesystem()` ends its failure
path with `return operation->second_flag ? 1 : 0`, but for `CBS_NODE_WRITE`
`second_flag` holds the written value's token kind, not `allow_failure`
(`src/parser.c` sets it at the `write` branch; only `copy`, `move` and `remove`
use it as a flag). `src/runtime.c` reads it the same way. Because
`execute_captured()` captures diagnostics and discards them on success, an
impossible `write` produces no output, exit 0, and an artifact silently missing
the file — reproducible with an ordinary absolute workspace. Filed separately;
it is a different root cause in different files and does not belong in this
commit.

## Docs

User manual §4 (`--staged` may be relative, and workspace spelling cannot
affect artifact bytes); integration contract (build invocation);
`docs/cpdl-test-coverage.md`.
