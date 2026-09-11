# Issue #131: phase progress and failure attribution

## Result

Implemented and tested in standalone CBS.

`CbsExecutionContext` now accepts a `CbsPhaseEvent` callback. CBS emits
`phase-begin` immediately before each phase and `phase-end` after it completes;
the end status is `0` for success and `1` for failure. A failed operation emits
the failed phase event before plan execution returns failure, so an embedder can
attribute the result without parsing recipe output.

The callback is optional and has no effect on ordinary CLI output. If an
embedder's event sink rejects an event, execution fails closed. The plan test
covers ordered begin/end events as well as the existing phase-capacity and
overflow behavior.
