# KOS_OP_IMPORT_FROM
- **Style**: EXTRA (`CONST`, `COUNT`, `CONST`, `CONST`). Row: `a0` = cpool module name str (or None), `a1` = level, `a2` = cpool tuple of attribute names, `a3` = cpool tuple of binding names (parallel to the attribute names; the same tuple object when there are no renames).
- **Stack**: no effect.
- **Semantics**: Import the leaf module, then for each attribute name `getattr` it and store the value into the current frame namespace under the corresponding binding name. Self-contained: no module on the stack, no compensating POP.
- **Submodule fallback**: when the `getattr` raises `AttributeError`, import `"{module.__name__}.{attr}"` and retry -- matching CPython's `from pkg import sub` behavior for submodules the package `__init__` does not import. Other exceptions propagate.
- **Invariants**: `a2` and `a3` are equal-length tuples of str.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(name, level, attr_names, bind_names)`.
- **Implementation**: `koskari/lib/opcode/IMPORT_FROM.c`

**Example:** `(import-from os path [sep as: s])` -- import `os`, bind `path` and bind `sep` as `s`. Extra row: `("os", 0, ("path", "sep"), ("path", "s"))`.

| index | opcode      | arg | arg view                       | comment |
|------:|-------------|-----|--------------------------------|---------|
| 0     | IMPORT_FROM | 0   | 0 ("os", 0, ("path","sep"), …) | bind "path", "s"; stack: [...] |
| 1     | CONST       | 1   | 1 (None)                       | statement value |
| 2     | RETURN      | 1   | 1                              | |
