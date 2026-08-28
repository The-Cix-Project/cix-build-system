# ADR-0017: Cix image composition

An image is an ordered manifest of verified CIXPKG identities plus inherited
base metadata. cixd materializes immutable versions; CBS supplies package
manifests and never mutates images in place.
