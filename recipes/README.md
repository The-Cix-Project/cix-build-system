# CBS qualification recipe

This directory contains the repository's build-tested CPDL fixture. The
historical migration drafts were deliberately retired after they identified
language and executor gaps; their findings remain in issue #144 and the
related backlog tickets.

## Execution status

[`zstd.cbs`](zstd.cbs) is built by `make upstream-test` and is the recipe
corpus's qualification fixture. It uses the source extractor's stable layout:
`${src}/<source-name>/...`.

The retired drafts covered Cix, GCC, the Linux kernel, libarchive,
squashfs-tools, TCC, and wireless-regdb. They were reference material, not
shipped build inputs. Their findings included source-layout handling, kernel
firmware-root and kconfig executor requirements, and the need for structured
build-image/capability/toolchain metadata. Those findings are tracked in #144
and the implementation tickets; the files are intentionally not part of the
shipped corpus.

CPDL supports exact file comparison through `require file ... { same_as ...
}`, so the old wireless-regdb note claiming that this assertion was missing
is retired as well.
