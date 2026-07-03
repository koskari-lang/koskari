# KOS_OP_IMPORT
- **Style**: EXTRA (`CONST`, `COUNT`, `COUNT`, `CONST`). Row: `a0` = cpool module name str (or None for a bare relative import), `a1` = level (leading dots; 0 = absolute), `a2` = leaf flag (0 = bind root, 1 = bind leaf), `a3` = cpool binding name str.
- **Status**: Implemented.
- **Stack**: no effect.
- **Semantics**: Import the module (root or leaf per the flag) and store it into the current frame namespace under the binding name. With the leaf flag clear the root package is bound while the full dotted chain is still imported — plain `(import foo.bar)` binds `foo`; with it set the deepest submodule is bound — the `(import [foo.bar as: fbar])` member form. Relative imports are always leaf-style. Self-contained: no stack effect, no separate VAR_BIND.
- **Leaf behaviour**: a NULL fromlist to `PyImport_ImportModuleLevelObject` returns the root while importing the whole chain; the sentinel `_import_fromlist = ("*",)` returns the leaf submodule.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(name, level, leaf_flag, bind_name)`.
- **Implementation**: `koskari/lib/opcode/IMPORT.c`

**Example:** `(import os.path)` — import the `os.path` chain, bind the root `os`. Extra row: `("os.path", 0, 0, "os")`.

| index | opcode | arg | arg view                  | comment |
|------:|--------|-----|---------------------------|---------|
| 0     | IMPORT | 0   | 0 ("os.path", 0, 0, "os") | bind "os"; stack: [...] |
| 1     | CONST  | 1   | 1 (None)                  | statement value; stack: [..., None] |
| 2     | RETURN | 1   | 1                         | |

Member form `(import [os.path as: p])` uses row `("os.path", 0, 1, "p")` to bind the leaf; relative `(import .foo)` uses `("foo", 1, 1, "foo")`.
