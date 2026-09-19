# KOS_OP_IMPORT_STAR
- **Style**: EXTRA (`CONST`, `COUNT`, `NOARG`, `NOARG`). Row: `a0` = cpool module name str (or None), `a1` = level.
- **Stack**: no effect.
- **Semantics**: Import the leaf module and bind its public names into the current frame namespace, matching Python's `from MODULE import *`. Name selection follows CPython's `import_all_from`: when the module defines `__all__` (a sequence of str; a non-str entry is a `TypeError`) exactly those names are bound; otherwise every `dir(module)` name that does not start with an underscore. Existing bindings are overwritten.
- **Compiler restriction**: the `import-all-from` special emits this opcode only at module top level; it is a compile error inside a function, lambda, `let`, or class body.
- **Invariants**: `a0` is a str or None.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(name, level, 0, 0)`.
- **Implementation**: `koskari/lib/opcode/IMPORT_STAR.c`

**Example:** `(import-all-from os.path)` -- bind `os.path`'s public names. Extra row: `("os.path", 0, 0, 0)`.

| index | opcode      | arg | arg view            | comment |
|------:|-------------|-----|---------------------|---------|
| 0     | IMPORT_STAR | 0   | 0 ("os.path", 0, …) | bind public names; stack: [...] |
| 1     | CONST       | 1   | 1 (None)            | statement value |
| 2     | RETURN      | 1   | 1                   | |

---

**Note:** There is **no** `KOS_OP_CAPTURE_FRAME` (or similar) in
`koskari/lib/opcodes.incl` today. Any future stack/frame introspection opcode
would be added there first, then documented here.
