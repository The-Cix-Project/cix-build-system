# Issue #283 validation: One escaping symlink rejects the whole source archive, which makes edk2 unpackageable

## Decision

A symbolic-link member whose target leaves the extraction root is skipped and
named, not fatal:

```text
warning[CPDL-W6001]: source: source `edk2`: member
  "edk2-master/EmulatorPkg/Unix/Host/X11IncludeHack": skipped: symbolic link
  target `/opt/X11/include` leaves the archive root
```

The confinement rule is unchanged, and this is the reason the scope could be
narrowed: the property being protected is "nothing is written outside the
extraction root", and never creating the link satisfies it exactly. Refusing
the archive protected nothing further while denying every legitimate member.
CBS remains stricter than `tar`, which creates the link and lets it dangle.

Both spellings that fail `safe_link_target` are skipped — an absolute target
and a relative target that climbs above the root. They are one rule with one
rationale, and splitting them would give one property two behaviours.

Three deliberate limits:

1. **Member selection still refuses.** A link named in an `extract { member … }`
   block is `CPDL-E6001`, because a recipe that asked for a member by name must
   receive it or an error, never silence.
2. **Hard links still refuse.** A hard-link target outside the root would
   expose external content rather than merely dangle, so it is untouched.
3. **The member is genuinely absent.** A recipe that needs the skipped link
   fails on its own `require`, so skipping cannot mask a real dependency.

A skipped link whose path is also a parent of later members resolves safely:
the link is never created, and `create_parents` makes an ordinary directory
inside the root for the members beneath it.

Reporting is a warning on the existing diagnostic channel rather than a
structured build event. `cbs_extract_archive` receives no `CbsExecutionContext`,
so an event would mean widening the public extraction signature; that is a
larger change than this issue needs and is not done here.

## Evidence

Measured against an archive reproducing the reported member shape
(`edk2-master/EmulatorPkg/Unix/Host/X11IncludeHack -> /opt/X11/include`, plus
`MdePkg/Base.h` and `README.md`), at `ced27bb` and with this change:

| | v0.1.105 | this change |
| --- | --- | --- |
| extraction | refused, `CPDL-E6001` | succeeds, one `CPDL-W6001` |
| `MdePkg/Base.h` | present (partial tree) | present |
| `README.md` | **absent** — extraction stopped at the link | present |
| `…/X11IncludeHack` | absent | absent |
| `cbs build` | `CBS-E1014`, no artifact | `built req2.cixpkg` |

The before-tree is worth recording: the old refusal aborted mid-extraction and
left a partially populated `$src`, so the failure was not even clean.

## Acceptance criteria

1. **An escaping link no longer denies the archive** — `tests/archive-test.c`
   asserts extraction succeeds for both the absolute (`/outside`) and climbing
   (`../../outside`) targets, that the link was not created, and that the
   archive's legitimate regular member survives alongside the skipped one.
2. **The skip is named, exactly once** — the test requires
   `warning[CPDL-W6001]`, the substring `skipped: `, and the target text, and
   fails if a second `CPDL-W6001` appears: links are walked in both extraction
   passes and only the link pass reports.
3. **A requested member still fails loudly** — `cbs_extract_archive_members`
   with the link named is asserted to return failure and to report
   `CPDL-E6001` with `rejected: `.
4. Each skip check extracts into a root of its own, so a file left by an
   earlier extraction cannot satisfy the survival assertion.

Unchanged and still asserted by the same test: refused member paths
(`../escape`), character devices, empty archives, safe links and hard links,
implicit parents, deferred directory modes, and UTF-8 pax paths.

## Docs

CPDL spec §4.5 extraction semantics and §8.2 code ranges — the `E6xxx` range
was in use by `src/archive.c` but undocumented, and is now specified alongside
the new `W6xxx` range, `CPDL-E6001`, and `CPDL-W6001`. User manual §5;
`docs/cpdl-test-coverage.md`.

## Not addressed

The reporting ticket notes that qemu bundles edk2 under `roms/` and hits the
same member. That follows from this change with no further work: the rule is
per-member and independent of how the member arrived. Whether qemu and OVMF
build once extraction proceeds is a recipe question for cix#584, not a CBS one.
