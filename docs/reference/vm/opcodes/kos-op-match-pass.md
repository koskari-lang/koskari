# KOS_OP_MATCH_PASS

- **Style**: EXTRA (RAW, RAW, CONST, RAW): mark depth, cursor depth, names
  tuple, group flag.
- **Stack**: `[...]` -> `[..., value]`; cannot miss.
- **Semantics**: Push one repeat pass's value. When any slot of the mark at
  depth `a0` is filled, the filled slots keyed by their names in `a2`: a dict
  when `a3` is 0, the captures as `MATCH_CAPTURES` gives them when `a3` is
  set. When no slot is filled, the span from the mark's position to the cursor
  at depth `a1`, as `MATCH_SPAN` gives it.
- **Invariants**: `a2` is a tuple; Code construction raises `ValueError`
  otherwise.
- **Compiler form:** Emitted by the match compiler at the end of each pass of
  a captured repeat, with `a3` set when the repeat's body is a group.
