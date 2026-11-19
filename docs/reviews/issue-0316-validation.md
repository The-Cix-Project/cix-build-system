# Issue #316 validation: manifest lines can be forged through a recipe path, a source URL and a tool alias

Opened while answering the owner's request to fix the manifest writer, which
#313 had recorded as a larger decision and left alone.

## The defect

The manifest is line-oriented. A value holding a line break ends its own `m`
line and begins another, in an artifact that still passes `cbs verify`. #313
closed the one reachable through an output's `license`. Measured on v0.1.130,
three more were open, each by building a recipe and reading `cbs list`:

```text
recipe path   "x\nm injected_by_path forged y.cbs"   -> metadata injected_by_path forgedy.cbs
source URL    "https://x.test/a\nm forged_by_url yes" -> metadata forged_by_url yes <sha>
tool alias    "cc\nm forged_by_alias yes"            -> metadata forged_by_alias yes
```

The recipe path is the composer's value, so no recipe rule can see it; the other
two were recipe values `validate` called valid. The `m` lines were written by
about twenty separate `fprintf` calls in `manifest.c` and `package.c`, so the
rule had to be remembered at each.

A fourth vector was found when the first fix was reviewed, and the first
version of this record had it wrong. It said a staged file name with a line
break "was already refused". The probe behind that forged a file that did not
exist, so it failed whatever the rule was. The discriminating cases:

```text
mkdir "${dest}/d\nm forged yes"      -> artifact written; cbs list shows `metadata forged yes`
write "${dest}/a\nm forged yes" "x"  -> artifact written; it then fails verification (CIXPKG-E4001)
symlink target "a\nm forged yes"     -> safe: a target is hex-encoded in the manifest
```

So a directory name forged a metadata line, and a file name wrote an artifact
that could not be verified. Nothing refused either.

## Fix

One writer, `cbs_manifest_line` in `manifest.c`, formats the line and refuses a
CR or LF in it, writing nothing and setting `errno` to `EINVAL`. Every `m` line
goes through it (a search of `src/` for every literal that begins an `m` line
found this one writer and the JSON emitters, which are not manifests), so a new
line cannot skip the rule without writing a raw `fprintf` where the convention
now forbids one. A refusal fails the build and writes no artifact
(`CPDL-E4001`, `provenance: a recorded value contains a line break`).

A staged path is refused where every entry is made, `add_entry`, with
`CPDL-E4007` and a message naming the cause, and the entry writer refuses it
again as the floor. Both use one helper.

The validator also refuses whitespace in a source URL and a tool alias, with a
located `CPDL-E3004`, so a recipe author sees the line and column and not a late
build failure. No new diagnostic code: all three use existing codes.

## Evidence

- `tests/manifest-line-test.sh`: a clean path builds and is recorded verbatim;
  a path with a line break fails the build, writes no artifact and names its
  cause; and a staged directory with a LF, a directory with a CR and a file with
  a LF each fail the same way, with a name containing a space as the control.
  The first half fails on the engine built from the `v0.1.130` tag, and the
  staged cases fail on the engine from the first commit of this fix, which had
  the writer and not the path check.
- `source-url-newline` and `tool-alias-newline` rejection fixtures with
  `.expect` text. The `v0.1.130` engine accepts both.
- The origin/main corpus, 2513 definitions when measured for this ticket: all
  still validate. None has whitespace in a source URL or a tool alias. Whether
  any staged path in a real build contains a line break was not measured: only
  recipes were validated, none was built.
- Parser and validation suite 90 cases, also under `tcc -b`.

## Not done

The writer refuses a line break and nothing else. Whitespace in a recorded
value that is not a line break (a space in the recipe path, for instance) is
recorded as given, because a path with a space is legitimate and the reader
takes the rest of the line as the value.
