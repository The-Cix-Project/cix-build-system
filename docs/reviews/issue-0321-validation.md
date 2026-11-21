# Issue #321 validation: A per-command `jobs N` is never checked against $jobs, though the specification says it must not exceed it

Found while fixing #320 and kept out of it, because it was a decision and not a
defect with an obvious fix. The owner decided it on 2026-11-21.

## The drift

The specification said: "`jobs N` declares the maximum concurrency the command
may use. `N` must be positive and must not exceed `$jobs`." The validator checks
only that `N` is positive or `$jobs`, and nothing at run time compares it with
anything. Measured on v0.1.133: `run "echo" { "hi" jobs 64 }` validates and
builds with exit 0 under `--jobs 2`. The rule came in with the CPDL 0.1 grammar
on 2026-08-28 and was carried into 1.0 on 2026-09-29; no ADR records a reason for
it, and it was never enforced.

## Why it could not stay

Until #320, `$jobs` was 1 on every host, so the rule would have forbidden every
`jobs N` above 1 and nobody could use it. Since #320, `$jobs` follows the host's
CPU budget, so the rule would have made a recipe valid on one machine and refused
on another. Validation must not depend on the host, so it could only have been a
run-time refusal, and one that fights a composer who passes a lower `--jobs` on
purpose, which cix does.

## What is read

Verified before the specification was edited to say so: the parser builds a
`CBS_NODE_RUN_JOBS` node and the validator checks it. Nothing in the plan, the
executor, the events, the report or `explain` reads it. The only reader of the
word `jobs` at run time is the `${jobs}` interpolation. So `jobs N` is validated
and otherwise inert.

## Decision

The rule is removed, not enforced:

- `jobs N` is accounting. `N` must be a positive integer or `$jobs`; CBS does not
  check it against `$jobs`, does not cap it and does not report it.
- How many jobs `$jobs` stands for is the caller's (`build --jobs N`) or, unset,
  the detected CPU budget. A recipe opts in to parallelism by passing `${jobs}` to
  its tool, as in `"-j${jobs}"`, and opts out by passing a fixed value or nothing.
- There is no separate "parallel-safe" declaration. It would state the same fact
  twice and could disagree with the argument actually passed.
- Refusing literals above 1 was considered and rejected: it restricts the values
  of a declaration nothing reads.

The corpus is unaffected. At `origin/main` `0cbf48ce`, 2628 definitions, the only
literal value is `jobs 1`, in 6 of them; 813 declare `jobs $jobs`, of which only
28 also pass `${jobs}` to a tool, and four bare `"-j"` arguments are
`objdump -j SECTION` and offset flags, not a build tool. If enforcement is wanted
later it should be a deliberate run-time check with its own diagnostic code, as a
new ticket.

## Evidence

A documentation change: `docs/spec/cpdl-1.0.md` and `docs/user-manual.md` now say
what the code does, and the release note records it. No code or test changed, so
no release was cut; the gate passes with the docs contract unchanged.
