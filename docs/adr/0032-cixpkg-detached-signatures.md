# ADR-0032: Use detached signatures for CIXPKG artifacts

## Status

Accepted

## Decision

CIXPKG carries integrity digests but no embedded signature field. Repository
publication uses a detached minisign signature over the complete artifact
bytes. The repository trust anchor and key rotation policy remain outside CBS;
CBS verifies format integrity, while the repository/orchestrator verifies the
detached signature before installation.

The trusted comment follows Cix's existing convention exactly:

```text
cix pkg <name>@<version> sha256=<hex>
```

The complete trusted comment is compared, in addition to verifying the
signature and the artifact digest. This keeps key rotation independent of
content-addressed artifact bytes and gives CBS and Cix one signature verifier.

## Consequences

Rotating a key does not change an artifact or invalidate immutable
`pkg_artifact_sha256=` approvals. A standalone `.cixpkg` is not
self-authenticating; its detached signature and trusted repository metadata
are part of the publication record. Installers must reject a publication that
has no required detached signature rather than probing another format or trust
path.

The former reserved 64-byte signature area is not part of CIXPKG v2: bytes
96–159 are the payload digest, and readers do not interpret them as a
signature.
