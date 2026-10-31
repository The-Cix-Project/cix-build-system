# Issue #310 validation: show every reserved keyword in the manual

After #309 I closed with a list of what the gate does not catch, and absences
were first on it: a construct the language has that the manual never shows. That
was a limit stated instead of a problem solved. The specification lists every
reserved keyword, so a script can require each of them to appear in the manual.

## What the check found the first time it ran

Five implemented constructs the user manual never mentioned: `links` with
`needs` and `forbids`, `patch`, `truncate`, the `nonempty` property of
`require file`, and the `checksums` and `signature` forms of `upstream verify`.
The reserved word `any` was also never mentioned. `links` was used by 14 corpus
recipes and had no prose in the specification either, only its grammar.

## The check was wrong three times before it was right

Each failure is a way "the word appears" lies, and each is why the rule is what
it is.

1. **Bare words.** The first run reported ten missing keywords, five of them
   English words (`down`, `gives`, `positions`) from the sentence above the
   list. The list is now read only from inside its fenced block.
2. **A word is not a construct.** `links` was not flagged though the operation
   was undocumented, because the manual says "links against libarchive". A
   keyword must appear as code, in a fenced block or an inline span.
3. **Span pairing and quoted errors.** Pairing backticks across a line with an
   odd count shifts every later span. And `errno=40 (Too many levels of symbolic
   links)`, quoted in backticks, made `links` look documented again. Spans are
   read per line on balanced backticks only, and a span containing a parenthesis
   (a quoted diagnostic) is skipped.

Run against the manual as it stood at `HEAD` before this work, the final rule
names nine absent constructs, `links` among them. Against the new manual it
passes. It fails closed: if the keyword list cannot be read, that is an error
and not a pass over nothing.

The residual limit is written into the code. A keyword used as an ordinary word
inside some other span can still satisfy the check, so it finds wholesale
absences and not thin explanations.

## Documenting them found two more problems

**The `links` failure paths had no test.** The only coverage was one positive
`needs` case in `fs-test.c`. Before writing failure behaviour into the manual I
ran it end to end, using a digest-keyed source cache to put a real ELF into
`${src}`. A missing needed library and a present forbidden one fail with
`CPDL-E4005`, as written, and the two passing cases pass.

On a file that is not an ELF object the operation correctly fails, but its
diagnostic named an unrelated cause: `errno=17 (File exists)` for a shell
script, `errno=2 (No such file or directory)` on another run.
`cbs_observe_dependencies` returned 0 for "not an observable ELF" without
setting `errno`, so the caller printed whatever an earlier call had left behind.
A user would chase the wrong thing. It now sets `ENOEXEC` wherever it refuses a
file's format, giving `errno=8 (Exec format error)`.

Writing that fix introduced a bug, caught on re-reading: my new condition read
`length` after a failed `fseek` had skipped the `ftell` that sets it. `length`
is now initialised to -1.

**Hand-written corpus counts.** The figure changed five times in one day. Dating
each copy did not stop it going stale. Exact counts are removed from `CLAUDE.md`
and the blocker register, which now say "past two thousand" and point at the
dated figures in the release notes and the review log, where being dated is what
makes an exact number acceptable.

## Acceptance criteria

`docs-contract-test.sh` fails when any reserved keyword is not shown as code in
the manual (checked against `HEAD`'s manual: nine failures; against the new one:
none), and when the list cannot be read.

`fs-test.c` gains four `links` cases: a missing needed library, a forbidden
library that is present, one that is absent, and a non-ELF file, which asserts
`cannot inspect ELF dynamic dependencies` and `Exec format error`. The last
fails against the old `observe.c`, at `fs-test.c:340`, and passes with the fix.

The manual documents each construct, with complete `cbs check` examples for
`truncate`/`patch` and for `links`, so the examples gate from #309 runs them.
The specification gains the missing prose for `links`.

Full gate green; all corpus recipes validate.

## Not changed, deliberately

`links` fails closed on any file with no dynamic section, a statically linked
executable included, even for `forbids`, where an absent library would arguably
satisfy the assertion. I confirmed it with a hand-assembled static-shaped ELF64.
That is a semantic decision and not a defect, so the manual states the behaviour
and the choice is left to the owner.

## What this still does not catch

A construct that is shown but explained badly, a wrong sentence about a
construct that is shown, and a feature added without a keyword. Those need a
reader.