# KOS_OP_MATCH_CLEAR

- **Style**: EXTRA (RAW, CONST): mark depth, slot indices.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Empty the slots of the mark at depth `a0` that the tuple `a1`
  lists.
- **Invariants**: `a1` is a tuple; Code construction raises `ValueError`
  otherwise. An index outside the mark's slots raises `SystemError`.
- **Compiler form:** Emitted by the match compiler where a branch of an
  `(or ...)` or an optional misses, emptying the slots that branch could have
  filled. The root captures of a `match` arm are its variables, which keep
  what the branch stored.
