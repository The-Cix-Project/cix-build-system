# ADR-0026: Structural build dependencies

Build dependencies are mandatory CPDL declarations grouped by role. CBS
validates compiler/tool/library names before execution; undeclared tools fail
validation and cannot be discovered from PATH.
