# KOS_OP_MATCH_SCOPE

- **Style**: EXTRA (RAW, RAW): cursor depth, slot count.
- **Stack**: `[...]` -> `[..., m]`; cannot miss.
- **Semantics**: Push a mark at the position of the cursor at depth `a0` with
  `a1` empty slots. A mark's position never moves.
- **Compiler form:** Emitted by the match compiler for each capture scope (a
  standalone matcher's root, a group, one repeat pass) with the scope's slot
  count; after an uncaptured repeat in a `match` arm, for the lists of its
  values; and with no slots, to record the position an `(or ...)`, an optional
  or a capture returns to or reads from.
