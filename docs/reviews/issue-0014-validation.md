# Issue #14 validation: confined archive extraction

CBS uses the admitted `libarchive` library through `cbs_extract_archive`; no
external extractor is invoked. Extraction accepts tar-family and ZIP formats,
rejects absolute/parent-traversal paths, links, device nodes, and unsupported
formats with `CPDL-E6001`. The TCC build links `-larchive`, and the regression
suite includes rejection of an invalid archive without destination mutation.
