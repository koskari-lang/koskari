# KOS_OP_MATCH_LOAD

- **Style**: EXTRA (LABEL, RAW, RAW): label, mark depth, slot index.
- **Stack**: `[...]` -> `[..., v]`; jump to `a0` with nothing pushed when the
  slot is empty.
- **Semantics**: Push the value in slot `a2` of the mark at depth `a1`. An
  index outside the mark's slots raises `SystemError`.
- **Compiler form:** Emitted by the match compiler after an uncaptured repeat
  in a `match` arm, once per name of the repeat, ahead of the store that
  writes the list of its values to the variable; the jump skips that store.
