# KOS_OP_IMPORT_FROM
- **Style**: CONST. Arg = constant pool index of attribute name string
  (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., module] → [..., module, value]`. Read named attribute from
  TOS **without consuming TOS**. The module remains on the stack.

> **PEEK-NOT-POP WARNING** — This is the **only** opcode in the Koskari
> instruction set that reads TOS without removing it. This behaviour allows
> multiple successive IMPORT_FROM instructions to extract different attributes
> from the same module. Every sequence of one or more IMPORT_FROM instructions
> **must** be followed by `POP 1` to remove the module from the stack.
> Omitting the POP leaves a dangling module reference that corrupts the stack.
>
> This invariant is **structurally enforced**: IMPORT_FROM is only ever emitted
> by `helper_import_from_sequence()` in `koskari/compile/specials/imports.py`,
> which always emits the compensating `POP 1` as its final instruction.
> Do not emit IMPORT_FROM anywhere else.

- **Invariants**: TOS must be a module or object with the named attribute.
  `cpool[arg]` must be a str. The module is **not popped**; the caller (always
  `helper_import_from_sequence`) is responsible for the compensating `POP 1`.
- **Compiler form:** Pre-compile arg = attribute name string; post-compile arg =
  constant pool index.
- **Implementation**: `koskari/lib/opcode/IMPORT_FROM.c`

**Example:** `(import-from os path sep)` — import `os`, extract `path` and `sep`.
Pool: 0 = `"os"`, 1 = `"path"`, 2 = `"sep"`, 3 = None.

| index | opcode      | arg | arg view    | comment |
|------:|-------------|-----|-------------|---------|
| 0     | IMPORT      | 0   | 0 ("os")    | stack: [..., os] |
| 1     | IMPORT_FROM | 1   | 1 ("path")  | PEEK TOS; stack: [..., os, os.path] |
| 2     | VAR_BIND    | 1   | 1 ("path")  | bind "path"; stack: [..., os] |
| 3     | IMPORT_FROM | 2   | 2 ("sep")   | PEEK TOS; stack: [..., os, os.sep] |
| 4     | VAR_BIND    | 2   | 2 ("sep")   | bind "sep"; stack: [..., os] |
| 5     | POP         | 1   | 1           | remove module; stack: [...] |
| 6     | CONST       | 3   | 3 (None)    | statement value |
| 7     | RETURN      | 1   | 1           | |

---

**Note:** There is **no** `KOS_OP_CAPTURE_FRAME` (or similar) in
`koskari/lib/opcodes.incl` today. Any future stack/frame introspection opcode
would be added there first, then documented here.


## 11. Exception handling

