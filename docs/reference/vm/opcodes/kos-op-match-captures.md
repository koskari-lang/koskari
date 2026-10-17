# KOS_OP_MATCH_CAPTURES

- **Style**: EXTRA (RAW, RAW, CONST): mark depth, cursor depth, names tuple.
- **Stack**: `[...]` -> `[..., caps]`; cannot miss.
- **Semantics**: Push the captures of the mark at depth `a0`: each filled slot
  under its name in `a2`, an empty slot absent. Over a tuple, list, other
  sequence or single value `caps` is a dict. Over a pair spine it is a
  `DatumFrame` whose position is the first cell's and whose source is the span
  from the mark's position to the cursor at depth `a1`: the first cell itself
  when the span is empty or runs to the spine's end, the car read through
  `Datum` for one cell, otherwise a tuple of the cars. Raises `ValueError`
  when the spine no longer reaches the cursor's cell from the mark's.
- **Invariants**: `a2` is a tuple; Code construction raises `ValueError`
  otherwise. A names tuple without one name per slot of the mark raises
  `SystemError`.
- **Compiler form:** Emitted by the match compiler at the end of a standalone
  matcher, for a named group, and at the end of each pass of an uncaptured
  repeat that captures.
