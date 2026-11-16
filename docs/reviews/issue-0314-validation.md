# Issue #314 validation: explain reports an output's own replaces

Reported by the cix maintainer from a measurement on 192.168.15.95 on
2026-11-15. CPDL accepts `replaces` inside an `output` block, but
`cbs explain --json` never emitted it: each `outputs[]` entry carried `name`,
`main`, `files`, `requires` and `abi`, and `replaces` appeared only at the top
level, from the package declaration.

## What was measured

cix declared `replaces { package "probe-rtx-b" }` inside an output. `explain`
accepted the recipe and reported nothing for it. Installing it into an image
where `probe-rtx-b` owns the path was then refused by cix's one-owner rule
(cix#553), because cixd reads `replaces` from `explain` only. With `replaces` on
the package instead, the install was refused the same way, and that refusal is
correct: the path belongs to the output, not to the main package.

It matters now because cix#680 moves `libgcc_s.so.1` and `libstdc++` into a
`gcc-libs` output of gcc, taking them from `squashfs-tools`, and that transfer
needs `replaces` on the output.

The same measurement on this repository, before changing anything:

```text
top-level replaces: ['old']
  p   main=True  keys=['name', 'main', 'files', 'requires', 'abi']
  a   main=False keys=['name', 'main', 'files', 'requires', 'abi']
```

It also showed that **no artifact records `replaces` at any level**, package or
output: a built `p` and `a` each carried zero `replaces` lines in their
manifests. So `replaces` is `explain`-only everywhere, which is consistent, and
the ticket's ask is the whole job. No artifact change was made, and none was
asked for.

## Decision

Each `outputs[]` entry carries its own `"replaces"` array, empty when it
declares none, in the same place `requires` sits. The main entry reports the
package's, as it does for `requires` and `abi`; an output reports its own and
**never inherits**, the rule `license` and `abi` already follow. The
authorization belongs to the entry whose files are taken over, so a transfer to
`cixctl` is readable on `cixctl`.

The top level built `replaces` inline. It is now one function,
`print_replaces_array`, used for the package and for each output, exactly as
`print_requires_object` was extracted for #299, so the two cannot drift. The
top-level output is unchanged.

## Acceptance criteria

`tests/multi-output-test.sh` extends the shared fixture so one build asserts all
three cases: the package declares `replaces { package "cix-legacy" }`, `cixctl`
declares `cix-old-client`, and `cix-tools` declares none. Then:

- `cixctl` reports `["cix-old-client"]`, the main entry reports
  `["cix-legacy"]`, and `cix-tools` reports `[]`, each matched as an exact
  substring of its own entry;
- `cix-old-client` appears exactly once in the whole document and `cix-legacy`
  exactly twice (the top level and the main entry), so an entry inheriting
  another's value would change a count.

The assertions fail against the v0.1.129 engine. The spec, the integration
contract, the embedding guide and the manual now say what each entry carries and
that the authorization belongs to the entry whose files are taken over.

## Found while editing

The embedding guide read "file selectors `requires` and `abi`" with a comma
missing. That was my own slip from #312, fixed with this change.

## Not changed, and why

`outputs[]` entries still carry no `license`, though an output may declare one
and it is recorded in that output's artifact. The ticket does not ask for it,
and whether cixd wants it is cix's to say, so it was raised with them and not
added unrequested.

## What this is an instance of

#313, this ticket, and the missing `license` are the same defect: an output's
item set is an implicit contract with several independent consumers (the parser,
the validator, the artifact writer and `explain`), each carrying its own list,
and nothing checks that they agree. A grid of the items each consumer handles
showed the zeros directly. That root cause is addressed separately, as a gate.