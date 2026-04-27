# Opcode handler fragments

This directory holds **one C source fragment per opcode** (e.g. `NOOP.c`, `RETURN.c`, `OBJECT_ADD.c`). These files are not compiled on their own; they are `#include`d by `eval.c` so that each opcode's handler (and any helpers used only by that opcode) lives in a separate file.

## How it works

- **handlers.incl** – Generated from `../opcodes.incl` by `tools/opimpl.py` (run `make impls` or `tox -e handlers`). Contains one `#include "opcode/NAME.c"` per OP in order. `eval.c` includes `handlers.incl`; the order matches the opcode enum, which the dispatch table and computed-goto labels rely on.

- **Fragment files** – Each `<NAME>.c` is a C fragment: optional GPL block, optional `/** ... */` docstring, optional **dedicated** helpers (used only by this opcode), and exactly one `KOS_OP_HANDLER(NAME) { ... }`. No `#include` or other boilerplate; the file is included in the middle of `eval.c` where `KOS_OP_HANDLER`, `CURRENT_*`, and shared helpers are already defined.

- **Shared vs dedicated helpers** – Helpers used by more than one opcode (e.g. `push_result`, `kos_check_cmp`, `op_get_next_from_iter`) live in `eval/eval_state.c` (included into `eval.c`). Helpers used by a single opcode (e.g. the `yield_from_*` helpers) live in that opcode's fragment (e.g. `YIELD_FROM.c`).

## File naming

Opcode `NAME` (as in `opcodes.incl`) → file `opcode/<NAME>.c` (case-sensitive).

Examples: `NOOP` → `NOOP.c`, `OBJECT_ADD` → `OBJECT_ADD.c`, `PUSH_FOREACH` → `PUSH_FOREACH.c`.

## Build

The extension is built from `koskari/lib/*.c` only; the opcode `*.c` files are not listed as sources. They are included only by `eval.c`, so no change to `setup.py` is required. `MANIFEST.in` includes `koskari/lib/opcode/*.c` so the sdist contains them for builds from source.
