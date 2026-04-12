# Compiler Architecture

The current Koskari compiler lives in `koskari/compile/`. Implemented special
forms, the macro pipeline (reader macros, `define-syntax`, **`compile/syntax-*`**
/ **`compile/macro-*`**), and compiler optimizations are documented in
[design/SPECIALS.md](../SPECIALS.md) (see § Macros, reader macros, hygiene, and
compiler optimizations).

For the historical python-sibilant (CPython bytecode) pipeline and the
sibilant-ceval migration notes, see
[design/archive/compiler-python-sibilant-historical.md](../archive/compiler-python-sibilant-historical.md).
