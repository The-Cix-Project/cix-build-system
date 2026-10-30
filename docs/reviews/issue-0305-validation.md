# Issue #305 validation: every input in the key can be given to the verb

Named by the cix maintainer while settling #303, and filed separately so that
ticket stayed about the finalizer.

## What was wrong

`cbs fingerprint` exists so a composer can compute a build's key *before*
building, for a cache lookup. That only holds for inputs the verb can be told
about. After #303 it took `--arch`, `--command-path`, `--library-path`,
`--input`, `--tool-identity` and `--finalize-command` — but not
`--prune-policy` or `--firmware-root`, and both enter the key:

- `--firmware-root DIR` is bound by content, through the same material walk as
  the tool directory.
- `--prune-policy FILE` contributes `prune.strip_debug`,
  `prune.drop_static_archives` and `prune.drop_libtool_archives`.

So both documents had to carry a caveat naming the exceptions. Closing this
deletes that sentence, which was the point: a parity claim with a list of
exceptions is a claim a reader has to check rather than rely on.

## Decision

The verb takes both, resolved exactly as a build resolves them.

**The prune policy is loaded, not named.** The key binds the three booleans the
file resolves to, so accepting the path and not reading it would predict
nothing — and would predict it *silently*, which is the failure shape this
repository keeps finding. `cbs_prune_policy_load` is the same call the build
makes, so an invalid rule is the same refusal (`CBS-E1007`) with the same
message.

**The firmware root is validated the same way**, so an absent or non-directory
root is `CBS-E1008` from the verb as it is from a build, rather than a key
computed over material that was never there.

Both got **explicit assignment branches**. `parse_fingerprint_options` ends in
a bare `else options->library_path = value;` — the same catch-all the build
parser has — so an option added to the space-form list without its own branch
is silently parsed as `--library-path`. The test asserts the space form and
the `=` form separately for exactly that reason.

## Acceptance criteria

`tests/fingerprint-parity-test.sh`, new and in the gate:

1. the verb's key equals the artifact's for a `--firmware-root` build, in both
   option forms;
2. changing a byte of the firmware content moves the build's key and the
   verb's **together**;
3. the same for `--prune-policy`, with two policies differing by one rule
   giving two keys, and the verb matching each;
4. a policy that resolves to one rule is not the same key as no policy at all,
   so an empty load is distinguishable from an absent one;
5. an absent firmware root is `CBS-E1008` and an invalid prune rule is
   `CBS-E1007` from the verb, not ignored.

It fails against the v0.1.123 engine.

Full gate green; all 1969 corpus recipes still validate.

## Scope

cixd does not read the build fingerprint at all: its cache identity is
name-version-release plus the recipe's approved `artifact_sha256`, and cix
ADR-0367 chose one approval line per artifact over a fingerprint-based
approval. So this closes a gap between CBS's documented contract and its
behaviour rather than unblocking a consumer, and it is recorded that way
instead of claiming an impact it does not have.
