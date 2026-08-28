# Issue #10 validation: native helper governance

- Issue: `#10 Define the governance rule for package-specific native helpers`
- Date: 2026-08-28
- Result: Pass
- Decision: [ADR-0004](../adr/0004-native-helper-governance.md)
- Registry: [native-helpers.md](../native-helpers.md)

ADR-0004 defines a refusal-capable rule: helpers are exceptional only when
CPDL cannot express the operation without weakening an invariant, the helper is
bounded and deterministic, maintainers approve it with security and
reproducibility reviews, and a threat/test/removal plan exists. Convenience
wrappers, duplicated CPDL primitives, host-utility fallbacks, second parsers,
and language escapes are explicitly refused.

The ADR assigns approval to two maintainers, records the audit in the requesting
issue and registry, and requires quarterly and per-release reviews. It also
defines repetition thresholds that create a CPDL generalization issue instead
of another helper.

The registry starts empty and records the initial repository/issue search. The
CPDL specification now points to this governance and registry rather than
leaving the helper exclusion dependent on an unresolved ticket.

No code or helper was added. The full warning-clean TCC test gate remains the
required regression check for this documentation-only decision.
