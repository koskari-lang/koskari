# KOS_OP_MATCH_NEXT

- **Style**: EXTRA (LABEL, RAW): label, cursor depth.
- **Stack**: `[...]` -> `[..., e]`; jump to `a0` with nothing pushed on a
  miss.
- **Semantics**: Pull the next element from the cursor at depth `a1` and
  advance the cursor. From a pair spine `e` is a `Datum` over the cell's car,
  read through `Datum`, carrying the cell's position; from a tuple, list or
  other sequence it is the item; from a single-value cursor, the value. A miss
  at the cursor's end, or when the spine cell at the cursor is no longer a
  pair.
- **Compiler form:** Emitted by the match compiler for each pattern that
  consumes one element, ahead of its test.
