# KOS_OP_MATCH_STORE

- **Style**: EXTRA (RAW, RAW, RAW, CONST): mark depth, slot index, cursor
  depth, names tuple.
- **Stack**: `[..., v]` -> `[...]`; cannot miss.
- **Semantics**: Pop `v` into slot `a1` of the mark at depth `a0`, replacing
  what the slot held. When the cursor at depth `a2` walks a pair spine, a
  `Datum` `v`, and each `Datum` in a tuple `v`, takes the name `a3[a1]` as its
  binding; a `Datum` from a tuple, list or single value is stored unchanged. A
  cursor depth `a2` of 0 names no cursor: `v` was tested where it lay, and is
  stored unchanged.
- **Invariants**: `a3` is a tuple with an entry at `a1`; Code construction
  raises `ValueError` otherwise. An index outside the mark's slots raises
  `SystemError`.
- **Compiler form:** Emitted by the match compiler for each capture, with the
  names of the capture's scope, but for the root captures of a `match` arm,
  which are stored into their variables.
