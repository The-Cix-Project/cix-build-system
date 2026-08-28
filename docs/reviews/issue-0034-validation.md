# Issue #34 validation: dependency-cycle gate

The stage-zero contract admits only TCC and the explicitly listed libraries;
CBS itself is never required by those libraries. The Makefile link graph is
therefore acyclic, and the existing clean TCC build is the regression gate.
