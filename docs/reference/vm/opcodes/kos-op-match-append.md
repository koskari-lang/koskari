# KOS_OP_MATCH_APPEND

- **Style**: EXTRA (RAW): list depth.
- **Stack**: `[..., v]` -> `[...]`; cannot miss.
- **Semantics**: Pop `v` and append it to the list at depth `a0`. Raises
  `TypeError` when that entry is not a list.
- **Compiler form:** Emitted by the match compiler at the end of each repeat
  pass.
