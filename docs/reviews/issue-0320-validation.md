# Issue #320 validation: $jobs is always 1: context.jobs is hard-coded and cbs_effective_jobs() is never called

Reported by cix on 2026-11-21 after a kernel build on an 8-core host, with the
build cgroup's `cpu.max` at `800000 100000`, ran `make -j1 bzImage modules`.

## The defect

`src/package.c` set `context.jobs = 1;` and nothing assigned it afterwards.
`cbs_effective_jobs()` (#17) existed to compute it from a requested count, a CPU
budget and an administrator limit, and had no caller. ADR-0001 says `$jobs` is
"policy chosen by CBS from CPU, memory, sandbox, and administrator limits", so
the policy was specified and never connected. Every recipe passing `"-j${jobs}"`
to a tool built single-threaded on every host.

## Fix

- **Default.** `${jobs}` is the CPU the build may use: the cgroup v2 `cpu.max`
  quota of the process's own cgroup and of each ancestor up to the mount, rounded
  up, smallest wins (`max` says nothing), and the `Cpus_allowed_list` affinity
  mask, whichever is smaller; 1 when neither can be read. The pure parts take
  their roots as parameters, so a test can fake them and no environment variable
  was added.
- **Override.** `cbs build --jobs N`, used exactly as given and not capped by what
  is detected. `cbs_effective_jobs()` would cap a request at the budget, so the
  caller could only lower the value; an override that cannot raise it is not one.
  A value that is not a positive integer is `CBS-E1025`, exit 2. `strtol`
  saturates on overflow and takes a sign and leading space, so only a plain run
  of digits that fits is accepted: `99999999999999999999`, `+3` and ` 3` are
  refused. A review caught that the first version accepted the first as
  `LONG_MAX`. `--jobs` is `build` only; `fingerprint` and `doctor` refuse it with
  `CBS-E1001`.
- **Library.** `cbs_build_standalone_with_jobs` and
  `cbs_build_standalone_outputs_with_jobs` take `jobs` (0 detects). The existing
  entry points keep their signatures and now detect, which is embedder-visible
  and is stated in `docs/library.md`. No layout changed, so `CBS_ABI_VERSION` did
  not.
- **`doctor`** prints the value and which source decided it.

The administrator limit in `cbs_effective_jobs()` has no source and is passed as
0; nothing in this change invents one.

## Not a build input

`${jobs}` is host policy, so it must not reach the build fingerprint, the
manifest or `explain`. Measured with a recipe that passes `"-j${jobs}"` to a
real command: the artifact's `build_fingerprint` is identical at `--jobs 1`,
`--jobs 4` and the default, and equals `cbs fingerprint`, which has no `--jobs`.
`explain` reports neither arguments nor jobs.

## Measured

- Real cgroups, via `systemd-run --user --scope -p CPUQuota=...`: 200% gave 2,
  100% gave 1, and unrestricted gave 4 (`nproc` and the affinity mask). The
  build wrote the same value `doctor` reported.
- Corpus at `origin/main` `0cbf48ce`, 2628 definitions, all validate. 28 pass
  `${jobs}` to a tool: 25 with `"-j${jobs}"` (31 occurrences) and 3 with
  `"--parallel=${jobs}"`. Three more bare mentions are comments in those same
  `cmake` recipes, not arguments. Each of the 28 goes parallel for the first
  time on upgrade; 813 more declare `jobs $jobs`, which is accounting, and 6
  write a literal `jobs 1`. `recipes/zstd.cbs` declares `jobs $jobs` and passes
  no `-j`, so the upstream qualification is not made parallel by this change; it
  was not run, `CBS_UPSTREAM_CACHE` being unset. `cbs.cbs`, CBS's own recipe, also
  only declares `jobs $jobs`, so its build stays serial.
- With the affinity mask the smaller source: `taskset -c 0,1 cbs doctor` reports
  2 from `cpu affinity`, where the host has 4.
- Not measured: whether those 28 are safe in parallel. They have only ever been
  built at `-j1`. `--jobs 1` holds the old behaviour.

## What cix said

cixd will pass `--jobs` explicitly rather than rely on detection inside a build
container. Its one measurement, from 2026-09-25 and since overtaken by a root
change, showed no cgroup2 mount in a build container, so detection would fall
back to the affinity mask there. The `800000 100000` it saw is a parent budget
shared by every build running at once, so detection could never know one build's
share. That is why the override wins outright, and why the manual says a composer
that runs builds side by side should pass `--jobs`.

## Evidence

`tests/jobs-test.c`: the `cpu.max` arithmetic, the affinity list, and the cgroup
walk against a faked tree, with six mutations of the detection code each caught
by a named assertion (rounding down, no ancestor walk, larger limit winning, a
v1 line accepted, `..` followed, `max` read as a limit).
`tests/jobs-option-test.sh`: both spellings and last position, an explicit value
not capped, five bad values refused with `CBS-E1025` and no artifact, the default
a positive integer equal to what `doctor` reports, and the fingerprint
unchanged. It fails on the v0.1.133 engine at `--jobs 3`.

## Found on the way

The specification says a per-command `jobs N` "must not exceed `$jobs`", and
nothing enforces it: `jobs 64` validates and builds under `--jobs 2`. With
`$jobs` now varying by host it cannot be a validation rule. Filed as #321, a
decision and not a defect with an obvious fix, and not changed here.
