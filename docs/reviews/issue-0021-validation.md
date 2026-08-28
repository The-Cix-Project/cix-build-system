# Issue #21 validation: TCC-only compiler enforcement

CBS rejects ambient `cc`, GCC, Clang, and C++ compiler names during CPDL
validation. The check is centralized with executable policy and prevents a
PATH-provided compiler from being selected silently. Make builds continue to
use TCC with warning errors enabled.
