# ADR-0031: Production pipeline replacement contract

CBS replaces the existing shell pipeline only when every production recipe has
an equivalent CPDL build, independently verified CIXPKG output, signed
provenance, sandbox/resource enforcement, rollback coverage, and operator
runbook sign-off. Package identity, dependency semantics, modes, ownership,
logs, and failure behavior are compatibility requirements. Unsupported legacy
behavior is an explicit migration exception, never a silent fallback.

Cutover is staged: shadow comparison, canary publication, approval, then full
publication. The shell path remains read-only during canary and is retired only
after rollback evidence is captured.
