# Issue #62 validation

`cbs_transaction` separates preparation from atomic commit and propagates
failure, preserving the previous image until commit succeeds. cixd remains the
owner of immutable image versioning and retention.
