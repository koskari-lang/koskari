# KOS_OP_MATCH_SPAN

- **Style**: EXTRA (LABEL, RAW, RAW): label, mark depth, cursor depth.
- **Stack**: `[...]` -> `[..., span]`; jump to `a0` with nothing pushed when
  the span is empty.
- **Semantics**: Push the elements from the position of the mark at depth `a1`
  to the cursor at depth `a2`: one element as `MATCH_AT` gives it, two or more
  as a tuple of them, a `Datum` per cell over a pair spine. Raises
  `ValueError` when the subject no longer holds the span.
- **Compiler form:** Emitted by the match compiler for a capture over a
  pattern whose width is not exactly one; the jump leaves the capture's slot
  empty.
