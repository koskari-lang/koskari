# KOS_OP_MATCH_TAIL

- **Style**: EXTRA (LABEL, RAW, RAW, RAW): label, cursor depth, suffix width,
  minimum.
- **Stack**: `[...]` -> `[..., span]`; jump to `a0` with nothing pushed on a
  miss.
- **Semantics**: Take the elements left at the cursor at depth `a1` but the
  last `a2`, push them as a span, and advance the cursor past them. A miss
  when fewer than `a2` elements remain or the span holds fewer than `a3`. Over
  a tuple, list or other sequence the span is a tuple of the items; over a
  single value, a one-element tuple of the value or the empty tuple. Over a
  pair spine it is a `Datum` over the rest of the spine when the span runs to
  the spine's end, a `Datum` over the spine's terminator when the span is
  empty, and otherwise a tuple of a `Datum` per cell; a spine cut inside the
  span raises `ValueError`.
- **Compiler form:** Emitted by the match compiler for a rest pattern
  (`*: rest`, `REST`).
