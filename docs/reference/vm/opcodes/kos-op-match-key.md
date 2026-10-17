# KOS_OP_MATCH_KEY

- **Style**: EXTRA (LABEL, CONST): label, key.
- **Stack**: `[..., o]` -> `[..., o, v]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the value under the constant key `a1` in the mapping `o`
  read through `Datum`. An absent key is a miss. The mapping is probed without
  inserting: a `dict` is read directly and any other mapping through its
  `get`, so a `defaultdict` neither inserts nor invents the key.
- **Compiler form:** Emitted by the match compiler for a literal key of a
  mapping pattern.
