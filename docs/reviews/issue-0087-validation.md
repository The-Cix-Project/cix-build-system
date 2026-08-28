# Issue #87 validation

The standalone CLI now supports `cbs build RECIPE.cbs --staged ROOT --output
FILE`, validating the recipe and producing a CIXPKG without cixd. Help output
documents the command and the TCC regression gate remains clean.
