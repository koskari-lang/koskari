# KOS_OP_MATCH_AT

- **Style**: EXTRA (RAW): mark depth.
- **Stack**: `[...]` -> `[..., e]`; cannot miss.
- **Semantics**: Push the element at the position of the mark at depth `a0`,
  as `MATCH_NEXT` gave it. Raises `ValueError` when the subject no longer
  holds that element: a list shrunk below the position, or a spine cell that
  is no longer a pair.
- **Compiler form:** Emitted by the match compiler for a capture over a
  pattern one element wide.
