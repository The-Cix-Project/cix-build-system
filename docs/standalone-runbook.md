# Standalone CBS runbook

Build CBS with `make`, validate and inspect a recipe with `./cbs check` and
`./cbs explain`, create a package with `./cbs build`, and verify it with
`./cbs verify`:

```text
make
./cbs check recipe.cbs
./cbs explain recipe.cbs
mkdir -p /tmp/cbs-workspace /var/cache/cbs/sources
./cbs build recipe.cbs \
    --arch x86_64 \
    --staged /tmp/cbs-workspace \
    --output package.cixpkg \
    --cache /var/cache/cbs/sources
./cbs verify package.cixpkg
./cbs extract package.cixpkg --into /tmp/cbs-extracted
```

The standalone build fetches declared `http://` and `https://` sources with
libcurl when they are not already cached. Every download is written to a
temporary file, verified against its declared SHA-256, and atomically moved
into the digest-keyed cache before extraction. Cache hits are therefore
offline-capable. The resulting CIXPKG contains both the manifest and the
regular-file bytes from the staged `dest` tree, compressed independently with
zstd and verified by `cbs verify`.

Keep source/cache roots on a trusted filesystem, review diagnostics, and retain
the input recipe and provenance alongside each artifact. cixd is optional;
when present it may provide centralized source transport policy, stronger
sandboxing, and image transactions through the adapter API.
