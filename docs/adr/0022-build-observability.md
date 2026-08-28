# ADR-0022: Build observability

Each build emits structured phase logs, captures stdout/stderr, records start
and completion timestamps, detects configured stalls, and reports concurrency
and job ceilings. cixd owns persistence and streaming; CBS supplies events and
never silently discards output.
