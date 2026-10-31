# Issue #311 validation: make the documentation gates answer to the source

An advisor review of #309 and #310 against the project's ethos (one source of
truth, no parallels, fail closed, say only what is true) found four places the
new gates fell short of it. None touches the engine; all are in the gate and
the documentation.

## 1. An example that validates might not build

`cbs check` validates, and the manual then tells the reader to build. The first
recipe does `mkdir "${dest}/usr/bin"` without `parents`, so it was built rather
than assumed: it builds, exit 0. There was no defect. It is the only tagged
example with no sources and so the only buildable one, so a second tag for one
block would have been a parallel mechanism. There is still one tag, and the
guide now says that validating is not building.

## 2. The keyword check read a copy, not the source

#310 requires every keyword in the specification's section 2.4 to appear in the
manual. But section 2.4 is a hand-maintained copy of what `src/parser.c`
accepts, and step 7 of "Adding a language construct" says to update it with
nothing checking that it happened. A keyword added to the parser and forgotten
in 2.4 would have been silent twice: the specification stale, and the manual
check never asking about it.

The gate now asserts that every word the parser tests with `is_word`,
`consume_word` or `word_is` is listed in 2.4. That closes the loop to `src/`,
which is authority number one. It fails closed: fewer than 90 extracted words is
an error, so a refactor of those helpers cannot leave it comparing nothing and
passing. Today it extracts 105 against the specification's 110.

The reverse direction is deliberately not asserted. `after`, `config`,
`directory`, `forbids` and `needs` are in the specification and are matched by
the parser some other way, through a ternary or a comparison on token text,
which the extraction cannot see. `CLAUDE.md` already records these.

## 3. Two spellings of the corpus procedure

`CLAUDE.md` carried one corpus-measurement block and the release guide another,
and only the guide's had been extracted and executed. `CLAUDE.md` now points at
the guide's step 3 and keeps the reason, so there is one copy.

## 4. The description of the fix overclaimed

The release note said the examples "are run" and the guide said the test "runs
the examples". `cbs validate` executes nothing, which is why the fence tag is
`check`. After a week in which prose repeatedly outran the code, the sentence
describing the fix should not do it. Both now say "checked", and the test's
header comment no longer says "run".

## Acceptance criteria

Against a complete copy of the tree (every tracked file, via `git ls-files`),
two seeded defects both fail with exit 1:

- a keyword `zzfuture` added to the parser fails, naming `zzfuture` and section
  2.4;
- the parser's helpers renamed so the extraction finds nothing fails on the
  90-word minimum.

An unmodified copy passes. My first attempt at this check was not evidence: the
scratch copy omitted `COPYING*` and `TRADEMARK.md`, link errors filled the three
lines I displayed, and the exit status I printed belonged to `head`. It was
redone with the whole tracked tree and the real exit status.

## Found while editing, and fixed at the cause

Rewrapping the guide's paragraph put an inline span that begins with three
backticks at the start of a line. Both the examples extractor and the link check
read that as a fence, and both tests failed at once, which is them working. The
paragraph now describes the tag as the info string `cbs check` and writes no
triple-backtick spans in prose, so the hazard is removed and not merely avoided
by a lucky line break.

Full gate green.

## Not done

Building more than one tagged example, since only one is buildable. And checking
section 2.4 against the grammar's productions as well as the parser, which would
need an EBNF parser and is not worth one for this.
