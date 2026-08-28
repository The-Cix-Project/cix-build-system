# Issue #61 validation

`cbs_verify_signature` provides one bounded repository-signature verification
boundary. Key storage and cryptographic policy remain supplied by the trusted
cixd/repository service; verification failure is propagated.
