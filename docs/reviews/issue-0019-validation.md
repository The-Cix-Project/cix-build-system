# Issue #19 validation: manifest ordering

The manifest entry type and comparator provide one canonical lexical path
ordering for package metadata. Entries carry path, type, mode, size, and digest
fields; callers sort before serialization. TCC regression tests assert ordering.
