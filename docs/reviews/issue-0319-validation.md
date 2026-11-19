# Issue #319 validation: links refuses a statically linked executable that has no dynamic section

Raised by the owner's instruction to do the three items left open after #314:
this was the third, "`links` on a static binary", which the owner had been told
needed an ADR (ADR-0039).

## The defect

`links` reads `DT_NEEDED`. The observer refused every ELF with no `PT_DYNAMIC`,
and the manual and the specification both documented that. Measured on v0.1.131
with real and hand-built files, through `cbs build` and `links`:

```text
                                       forbids "libc.so.6"   needs "libc.so.6"
static-pie  (/sbin/ldconfig)                  passes          fails, CPDL-E4005
static ET_EXEC (no dynamic section)    fails, CPDL-E4004      fails, CPDL-E4004
```

A static-pie has a dynamic section and no `DT_NEEDED`, so it already answered
correctly. A static executable without one is the same fact, a file that loads
no shared library, and it was refused: the answer depended on how the linker laid
the file out, not on what it linked. The corpus is not affected either way:
14 definitions use `links`, 13 with `needs` and one with `forbids`
(`btop@1.4.7-3`, dynamic), measured at `origin/main` `844fa10b`.

## Fix

A file with no `PT_DYNAMIC` is observed as having no dependencies when its
headers say completely that it is static: type `ET_EXEC`, at least one `PT_LOAD`
and no `PT_INTERP`. `forbids` then passes and `needs` fails with `CPDL-E4005`.
Everything else is refused as before. The rule and its rejected alternatives are
ADR-0039.

The change only turns failures into passes. A file with a `PT_DYNAMIC` is read
exactly as before. The one visible difference is the diagnostic for `needs` on a
static file, `CPDL-E4004` becoming `CPDL-E4005`; the exit status is 3 either way.

## Evidence

- `tests/observe-test.c` builds synthetic ELF64 files: a static executable, and
  the same with a `PT_GNU_STACK`, are accepted; and each look-alike is refused,
  namely an executable that names an interpreter but has no dynamic section, a
  shared object without one, a relocatable object, a file with no program
  headers, an executable with nothing loadable, and a header table that runs past
  the end of the file. Loosening each condition in the rule (ignore the
  interpreter, accept a shared object, drop the loadable-segment requirement)
  makes the matching refusal case fail, and removing the static rule entirely
  makes the two accepted cases fail.
- `tests/fs-test.c` asserts the user-visible behaviour through `links`: `forbids`
  passes on a static executable and `needs` fails with `CPDL-E4005` naming the
  library. It fails at that line against the previous observer.
- End to end through `cbs build`, the real static-pie and the hand-built static
  `ET_EXEC` now answer identically: `forbids` rc 0, `needs` rc 3 with
  `CPDL-E4005`.
- Parser and validation suite, 90 cases, and the observer test, also pass under
  `tcc -b`.

## Found while testing

A first version of the change also required `ET_EXEC` or `ET_DYN` of every file
before looking for a dynamic section. A mutation of the rule showed that check
was redundant on the static path, which already requires `ET_EXEC`, and that
outside it, it could only turn a pass into a failure, which would have made
"this only turns failures into passes" false. It was removed, so a file with a
`PT_DYNAMIC` is read exactly as before.

## Not done

`forbids` is a statement about `DT_NEEDED`. It says nothing about code a static
binary carries inside itself, so `forbids "libc.so.6"` passes on a binary with
libc linked in. A recipe that means "no libc code in this file" cannot say it
with `links`, and could not before. The headers are trusted: a file crafted to
look static is observed as static, which is true at the ELF level.
