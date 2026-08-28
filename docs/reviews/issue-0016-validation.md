# Issue #16 validation: sandbox and dependency observation decision

ADR-0007 states the v1 guarantees and explicitly assigns kernel isolation to
cixd. It also makes static ELF dependency observation mandatory for declared
runtime dependencies, addressing the confirmed PAM omission case. This ticket
changes policy only; implementation follows in issues #17 and #18.
