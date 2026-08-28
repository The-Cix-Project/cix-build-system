# Issue #58 validation

`cbs_sandbox_run` provides one enter/leave hook boundary for cixd's production
sandbox backend. CBS does not create a competing namespace implementation;
failure to enter or leave is propagated as a hard failure.
