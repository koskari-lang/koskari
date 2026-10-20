# KOS_OP_MATCH_SCOPE_NEXT

- **Style**: EXTRA (LABEL, RAW, RAW): label, cursor depth, slot count.
- **Stack**: `[...]` -> `[..., m, e]`; jump to `a0` with `m` pushed on a
  miss.
- **Semantics**: `MATCH_SCOPE a1 a2` then `MATCH_NEXT a0` on the same cursor,
  one entry deeper under the mark: push a mark at the position of the cursor
  at depth `a1` with `a2` empty slots, then the cursor's next element, a
  `Datum` over a pair spine. At the cursor's end the mark stays and the
  instruction jumps to `a0`.
- **Compiler form:** Fused by the bytecode processor from a `MATCH_SCOPE`
  followed by a `MATCH_NEXT` on the cursor the mark was made at, as a
  matcher's root and a repeat pass open; both instructions are also emitted
  apart.
