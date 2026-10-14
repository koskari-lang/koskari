# KOS_OP_MATCH_FORM_VAR / KOS_OP_MATCH_FORM_FAST

- **Style**: EXTRA (LABEL, slot, RAW)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: Peek. The sequence gate on `elem` read through `Datum`: `str`,
  `bytes` and `bytearray` are atoms and miss; a pair spine, `nil`, a list, a
  tuple or a sequence-typed object descends. A sequence must have a length
  within the packed widths of `a2` (minimum in the high eight bits, maximum in
  the low, 255 for unbounded). A pair spine is not counted here; `MATCH_END`
  and the pulls judge its length. The cursor into the walk goes into slot `a1`:
  the pair or `nil` itself, or a cursor object over the sequence.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
