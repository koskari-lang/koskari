# Opcode handler fragments

This directory holds **one C source fragment per opcode** (e.g. `noop.c`, `return.c`, `object_add.c`). These files are not compiled on their own; they are `#include`d by `eval.c` so that each opcode’s handler (and any helpers used only by that opcode) lives in a separate file.

## How it works

- **`includes.incl`** – Lists every opcode fragment in the same order as `OP(name, style)` in `../opcodes.incl`. `eval.c` does a single `#include "opcode/includes.incl"` and the preprocessor pulls in each `opcode/<name>.c` in that order. The dispatch table and computed-goto labels in `eval.c` are generated from `opcodes.incl`, so the include order **must** match it exactly.

- **Fragment files** – Each `<name>.c` is a C fragment: optional GPL block, optional `/** ... */` docstring, optional **dedicated** helpers (used only by this opcode), and exactly one `KOS_OP_HANDLER(NAME) { ... }`. No `#include` or other boilerplate; the file is included in the middle of `eval.c` where `KOS_OP_HANDLER`, `CURRENT_*`, and shared helpers are already defined.

- **Shared vs dedicated helpers** – Helpers used by more than one opcode (e.g. `push_result`, `kos_check_cmp`, `op_get_next_from_iter`) remain in `eval.c`. Helpers used by a single opcode (e.g. the `yield_from_*` helpers) live in that opcode’s fragment (e.g. `yield_from.c`).

## File naming

Opcode `NAME` (as in `opcodes.incl`) → file `opcode/<lowercase_name>.c`.

Examples: `NOOP` → `noop.c`, `OBJECT_ADD` → `object_add.c`, `PUSH_FOREACH` → `push_foreach.c`.

## Regenerating `includes.incl`

If opcodes are added, removed, or reordered in `../opcodes.incl`, regenerate `includes.incl` so it stays in sync. From the repo root:

```bash
python3 -c "
import re
from pathlib import Path
path = Path('koskari/lib/opcodes.incl')
content = path.read_text()
ops = re.findall(r'^OP\(([A-Za-z0-9_]+)\s*,', content, re.MULTILINE)
lines = ['#include \"%s.c\"' % op.lower() for op in ops]
Path('koskari/lib/opcode/includes.incl').write_text(
    '/* Generated from opcodes.incl - one #include per opcode in order */\n\n' + '\n'.join(lines) + '\n'
)
"
```

Or maintain the list by hand when opcodes change.

## Build

The extension is built from `koskari/lib/*.c` only; the opcode `*.c` files are not listed as sources. They are included only by `eval.c`, so no change to `setup.py` is required. `MANIFEST.in` includes `koskari/lib/opcode/*.c` so the sdist contains them for builds from source.
