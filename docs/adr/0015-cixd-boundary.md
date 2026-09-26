# ADR-0015: CBS and cixd boundary

## Superseded in part by the v0.1.70 integration contract

The ownership split remains valid, but the original wording overstates the
integration mechanism. The supported first slice is cixd invoking the `cbs`
CLI as a child process inside the cixd-created container. CBS also provides a
static library API for deliberate direct embedding; it does not provide a
shared plugin or dynamic loader boundary. See
[`docs/integration-contract.md`](../integration-contract.md).

cixd owns REST, authentication, image transactions, and lifecycle. CBS owns
CPDL validation, execution, artifact creation/verification, and the event
stream consumed by an integration.
