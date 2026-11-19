# Issue #318 validation: a privileged declaration that matches no staged entry is accepted

Found while writing the package-items table (#317) and held back on purpose:
refusing it would make builds that succeed today fail, so it waited for cix.

## The defect

The specification says a `privileged file` declaration's "path must resolve to
one file beneath the staged destination" and "the staged entry must match it
exactly". Only the converse was enforced: a setuid entry with no declaration was
refused (`CPDL-E4007`), while a declaration matching nothing built with exit 0.
Measured on v0.1.132:

```text
declared 04711, staged 0755                       -> built, artifact mode 0755
declared 04711 for a path that does not exist     -> built
```

An allowance is a statement about what the artifact may contain. One that no
longer describes it, because the path was mistyped or upstream changed the mode,
is silent until the real file is refused, and nothing says which half was wrong.

## Fix

After the staged tree is collected for a manifest, every declaration must name a
regular file staged at exactly the declared mode. Otherwise the build fails with
`CPDL-E4007` and writes no artifact, saying which of the three it is: no such
staged path, not a regular file, or a different mode. The check runs at the three
places a manifest is built, including the multi-output partition, and uses an
existing code, so no code was added.

## Measuring the risk

The risk was builds that succeed today failing. cix reported that its corpus has
six declarations, all unconditional single fixed paths, and that a refusal was
fine if they build; neither side had measured that. Measured here:

- the corpus at `origin/main` `056e97ec` still has exactly 6 declarations;
- five have published artifacts, and `cbs list` of each shows the file at the
  declared path and mode: openssh `-14` and `-15` (`usr/libexec/ssh-keysign`
  4711), linux-pam `-13` (`sbin/unix_chkpwd` 4755), and probe-privmkdir `3` and
  `4` (`usr/libexec/probe/fakesuid` 4711);
- `probe-privmkdir@2` has none, so it was built with the new engine from its real
  source (fetched, and its SHA-256 matched the recipe's pin): exit 0, the file
  staged 4711. `@3` and `@4` were built the same way and also exit 0;
- openssh `-13`, whose recipe is no longer in the corpus, has the same file at
  the same mode in its published artifact.

So none of the six changes. Not measured: a rebuild of openssh or linux-pam
against the new engine, which needs their full build dependencies. The published
manifests are what those builds staged, so a rebuild that reproduces them cannot
trip the check.

## Evidence

`tests/privileged-declaration-test.sh`: a matching declaration builds and keeps
mode 4711 in the manifest (the control); and a missing path, a different mode,
different setuid bits, a directory and an undeclared setuid file are each refused
with no artifact and a message naming the cause. It fails on the v0.1.132 engine
at the first refusal. Under `tcc -b` it passes, as does the parser and
validation suite.
