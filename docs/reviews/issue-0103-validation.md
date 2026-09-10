# Issue #103 validation

`cbs_prepare_sources` composes cache-first source fetching, digest verification,
archive-format validation, and confined extraction into the standalone source
preparation boundary. The CLI now supplies an HTTP/HTTPS libcurl callback on
cache misses. Existing source and archive tests remain TCC-clean.
