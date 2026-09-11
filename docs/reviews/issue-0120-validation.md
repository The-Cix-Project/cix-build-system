# Issue #120 validation: transaction boundary

The former `cbs_transaction` function was removed. Its prepare/commit-only
signature could return failure after a partial commit and had no rollback
callback or prior-image model, so retaining it would provide false safety.

Standalone CBS is not the owner of image installation transactions. The
orchestrator owns versioned images, atomic publication, and rollback; CBS
produces and verifies artifacts for that boundary. The incomplete transaction
API is no longer compiled or exported.
