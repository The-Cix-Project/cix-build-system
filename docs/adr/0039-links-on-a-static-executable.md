# ADR-0039: `links` on a statically linked executable

- Status: **Accepted**, 2026-11-19, on the owner's instruction to do the three
  open items after being told this one needed an ADR. The owner did not
  otherwise review the rule below and can supersede it.
- Date: 2026-11-19
- Issue: cix-build-system#319

## Context

`links` reads the `DT_NEEDED` entries of a staged file and checks them against
`needs` and `forbids`. The observer, `cbs_observe_dependencies`, refused any ELF
with no `PT_DYNAMIC`, and the manual and the specification both said so: a
statically linked executable "fails with `cannot inspect ELF dynamic
dependencies`", because an assertion that cannot be evaluated is not satisfied.

That rule is right for a script, a truncated file or an object the observer
cannot read. It is wrong for a static executable, which can be evaluated: it
loads no shared library. Measured on v0.1.131, the answer depended on how the
linker happened to lay the file out, not on what it linked:

```text
                                       forbids "libc.so.6"   needs "libc.so.6"
static-pie  (/sbin/ldconfig, has a            passes          fails, CPDL-E4005
 dynamic section, no DT_NEEDED)
static ET_EXEC (no dynamic section)    fails, CPDL-E4004      fails, CPDL-E4004
```

`forbids` on a binary that links nothing is true. Refusing it is a false
refusal, and the recipe has no way to say what it meant. The corpus has 14
definitions that use `links`, 13 with `needs` and one with `forbids`
(`btop@1.4.7-3`, a dynamic executable), so no recipe in it is affected either
way.

## Decision

A file with no `PT_DYNAMIC` is observed as having no dynamic dependencies when
its headers say, completely, that it is a static executable:

- its type is `ET_EXEC`, and
- it has at least one `PT_LOAD` segment, and
- it has no `PT_INTERP` segment.

`forbids` then passes and `needs` fails with `CPDL-E4005`, naming the missing
library, like any other file that lacks it.

Everything else is refused as before, with `CPDL-E4004`:

- not an ELF, not ELF64, or not little-endian;
- a program header table that is out of range or runs past the end of the file;
- no `PT_DYNAMIC` and a `PT_INTERP`: a dynamic executable that lost its dynamic
  section, which must not be taken for a static one;
- no `PT_DYNAMIC` and a type other than `ET_EXEC`: a shared object without a
  dynamic section is not a shared object, and a relocatable object is not linked
  at all;
- no `PT_LOAD`: an executable with nothing to load is not one.

A file that has a `PT_DYNAMIC` is read exactly as before. This ADR does not
touch static-pie, which already behaved this way, and does not change what a
dynamic file with a malformed dynamic section does.

## Consequences

- The change only turns failures into passes. A build that succeeds today still
  succeeds: `forbids` on a file that was refused now passes, and `needs` on it
  still fails. The one visible difference is the diagnostic for `needs` on such
  a file, `CPDL-E4004` becoming `CPDL-E4005`; the exit status is 3 either way.
- `forbids` is a statement about `DT_NEEDED`, not about the C library. A static
  binary that carries its own libc passes `forbids "libc.so.6"`, because it
  links no shared one. A recipe that means "no libc code in this file" cannot
  say it with `links` and could not before.
- The observer trusts the headers. A file crafted to look static (no dynamic
  section, no interpreter) is observed as static, and that is true at the ELF
  level: nothing in it asks a loader for a library. `links` asserts what a built
  object is linked against, not that the object is honest.
- `cbs_observe_dependencies` is the single place the rule lives, and the manual
  and the specification state it in the same words.

## Alternatives rejected

- **Keep failing closed and document `forbids` as "forbidden or unobservable".**
  Honest, and it is what the manual said, but it leaves a recipe author who
  builds a static binary unable to assert anything, and it already disagrees with
  static-pie.
- **Accept any ELF with no `PT_DYNAMIC`.** This is the version that cannot tell
  a static executable from a dynamic one whose dynamic section is damaged, a
  shared object, or a relocatable object. `PT_INTERP` and `e_type` are what
  distinguish them, and they cost nothing to read.
- **A recipe flag such as `static`.** It would make the author state what the
  file already says, and a wrong flag would be a way to pass an assertion on a
  file that does link a library.
