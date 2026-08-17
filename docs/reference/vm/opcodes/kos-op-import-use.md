# KOS_OP_IMPORT_USE
- **Style**: EXTRA (`CONST`, `COUNT`, `CONST`, `CONST`). Row: `a0` = cpool module name str (or None), `a1` = level, `a2` = cpool tuple of feature stems, `a3` = cpool tuple of bind stems (parallel to the feature stems; the same tuple object when there are no renames).
- **Status**: Implemented.
- **Stack**: no effect.
- **Semantics**: Import the leaf module, then for each feature stem probe its candidate set — the stem itself, `compile/{stem}`, and `layout/{stem}` — and store each hit into the current frame namespace under the parallel bind stem with the same facet prefix applied, so a rename substitutes the whole family. A stem already carrying a facet prefix is literal: one candidate, bound as written. Self-contained: no module on the stack, no compensating POP.
- **Miss handling**: a per-facet miss is tolerated. The bare candidate goes through the same member fetch as `IMPORT_FROM`, keeping the on-demand submodule fallback; its crafted missing-member `ImportError` counts as a miss, while a `ModuleNotFoundError` from inside a submodule's own import propagates. A stem with zero hits raises `ImportError` naming the spellings tried.
- **Invariants**: `a2` and `a3` are equal-length tuples of str.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(name, level, stems, binds)`.
- **Implementation**: `koskari/lib/opcode/IMPORT_USE.c`

**Example:** `(use tacotruck tacos)` where the module defines `tacos` and `compile/tacos` — import the module, bind both. Extra row: `("tacotruck", 0, ("tacos",), ("tacos",))`.

| index | opcode     | arg | arg view                            | comment |
|------:|------------|-----|-------------------------------------|---------|
| 0     | IMPORT_USE | 0   | 0 ("tacotruck", 0, ("tacos",), …)   | bind "tacos", "compile/tacos"; stack: [...] |
| 1     | CONST      | 1   | 1 (None)                            | statement value |
| 2     | RETURN     | 1   | 1                                   | |
