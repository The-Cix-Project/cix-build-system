# Issue #296 validation: a `materialize` destination refusal is reported against the source

## Cause

`cbs_execute_materialize()` had one failure label naming `operation->value`
unconditionally. Four refusals reach it. Three are genuinely about the source —
an unresolvable reference, an undeclared source, a source that is not a regular
file or is a symlink — and `value` is right for those. The fourth is about the
destination, and inherited both the source's operand and the source's wording:

```text
error[CPDL-E4004]: runtime: verified source cannot be materialized: `$source.m`
```

for `materialize $source.m to "${build}/../../escape"`, where the source is a
declared, verified regular file and the destination is what was refused.

The sentence was the worse half. It asserts the source is the problem, sending
an author to check something that is fine, and the confinement refusal — the one
a real mistake is most likely to hit — was the misattributed case.

## Decision

The operand and the reason are chosen at the site that refuses, not from the
node kind:

```c
    const char *reported = operation->value;
    const char *reason = "verified source cannot be materialized";
    ...
    if (destination == NULL || !safe_parents(destination, root)) {
        free(destination);
        reported = operation->second_value;
        reason = "materialize destination cannot be resolved in the confined "
                 "roots";
        goto failure;
    }
```

Per-site rather than #287's kind-aware helper, because `materialize`'s two
operands have fixed and *different* roles. For the filesystem operations in
#287, which operand to name follows from the kind; here it follows from where
execution got to. A helper would have to encode "how far did we get", which the
control flow already knows.

`copy_one`'s failure further down already named `second_value` through its own
`fs_error` call, and is unchanged.

## Evidence

| recipe | v0.1.115 | this change |
| --- | --- | --- |
| `… to "${build}/../../escape"` | `verified source cannot be materialized:` `` `$source.m` `` | `materialize destination cannot be resolved in the confined roots:` `` `${build}/../../escape` ``; `errno=1` |
| `… to "${build}/f/child"`, parent a regular file | same, `` `$source.m` `` | same message, `` `${build}/f/child} `` ; `errno=20 (Not a directory)` |
| source refusals | `` `$source.…` `` | unchanged |

The two destination causes are now distinguishable by errno — `EPERM` for a
path outside the roots, `ENOTDIR` for a parent that is not a directory — which
#287 made possible by splitting `ELOOP`.

## Acceptance criteria

1. **A destination refusal names the destination** — `tests/fs-test.c` asserts
   the captured diagnostic contains the destination path and the word
   `destination`, and **does not contain** the source reference. The negative
   half is the one that would have passed before.
2. **A source refusal still names the source** — asserted to contain
   `$source.nosuch` and `verified source`.
3. The five refusals from #295 still all refuse.

Verified by restoring the single-operand failure label in a scratch build: the
new assertions fail there and pass with the fix.

## Not changed

The wording of the three source refusals. They were already correct, and
rewording them would churn text that `#295`'s assertions now pin.
