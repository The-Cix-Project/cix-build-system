# ADR-0010: Repository metadata and installation policy

- Status: Accepted
- Date: 2026-08-28

Trusted repository metadata is signed by the Cix repository key and contains
canonical identity, artifact digest, recipe digest, dependencies, and manifest
digest. It is distributed through the existing artifact-server URL and bearer
token model; CBS does not create a competing repository protocol.

Upgrades select one canonical identity and replace it only after the new
artifact verifies. Conflicts are hard errors unless the repository metadata
declares an explicit replacement. Installation is transactional: stage and
verify everything first, atomically switch the image/root reference, and leave
the prior version intact on failure. Garbage collection and key rotation are
deferred to cixd/repository services.
