# Issue #12 validation: verified source fetching

## Decision

CBS exposes a small `CbsFetchService` callback boundary. The standalone CLI
supplies a libcurl-backed callback by default; cixd may supply its own callback
when centralized transport policy is required. Each source is resolved
cache-first using its SHA-256 digest as the cache key. A cache miss tries
declared mirrors in order, writes to a temporary file, verifies the digest, and
atomically renames the verified file into the cache. A cached build therefore
does not require a network transfer.

## Validation

- `make clean && make` completes with TCC, `-Wall -Wextra -Werror -pedantic`.
- `make test` passes parser/validation and all execution, identity, source,
  dependency, and fetch tests.
- `tests/fetch-test.c` proves initial mirror fetching, digest-keyed cache reuse
  with networking disabled, and diagnostics containing source, URL, and cause
  for a failed verification/fetch.
- ADR-0005 remains the authority for transport and TLS ownership.

## Operational contract

The cache directory is provisioned by the caller (the CLI workspace or an
explicit `--cache` directory). Fetch callbacks receive a temporary destination
and must report a concise cause in the supplied error buffer when they cannot
provide the requested bytes.
