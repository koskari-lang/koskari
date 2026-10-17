# KOS_OP_MATCH_SAME

- **Style**: EXTRA (LABEL, RAW, RAW): label, mark depth, slot index.
- **Stack**: `[..., e]` -> `[...]`; jump to `a0` on a miss, `e` popped either
  way.
- **Semantics**: `e` must equal the value in slot `a2` of the mark at depth
  `a1`, both read through `Datum`: the same object, or equal under `==`. An
  empty slot is a miss, and so is a `DatumFrame` on either side.
- **Compiler form:** Emitted by the match compiler for a back-reference
  (`SAME`) to a name a scope of the pattern captures. A back-reference whose
  name no enclosing scope binds always misses.
