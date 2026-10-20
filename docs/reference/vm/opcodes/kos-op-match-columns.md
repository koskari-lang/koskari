# KOS_OP_MATCH_COLUMNS

- **Style**: EXTRA (RAW, RAW, CONST, CONST): list depth, mark depth, names
  tuple, slot indices.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: For each position `j`, store into slot `a3[j]` of the mark at
  depth `a1` the list of the values the name `a2[j]` takes in the passes of
  the list at depth `a0`, in pass order. A pass that did not bind the name
  adds nothing; when no pass binds it the list is empty. The list at `a0` is
  read, not changed.
- **Invariants**: `a2` and `a3` are tuples; Code construction raises
  `ValueError` otherwise. Tuples of different lengths, or an index outside the
  mark's slots, raise `SystemError`.
- **Compiler form:** Emitted by the match compiler after an uncaptured repeat
  that captures, giving each of its names the list of its per-pass values in
  the enclosing scope.
