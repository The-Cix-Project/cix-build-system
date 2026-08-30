# Standalone CBS runbook

Build CBS with `make`, validate a recipe with `./cbs check recipe.cbs`, create
a package with `./cbs build recipe.cbs --arch x86_64 --staged ROOT --output package.cixpkg`,
and verify it with `./cbs verify package.cixpkg`. Keep source/cache roots on a
trusted filesystem, review diagnostics, and retain the input recipe and
provenance alongside each artifact. cixd is optional; when present it may
provide stronger sandboxing and image transactions through the adapter API.
