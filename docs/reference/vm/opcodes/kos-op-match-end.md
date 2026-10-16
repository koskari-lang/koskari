# KOS_OP_MATCH_END_VAR / KOS_OP_MATCH_END_FAST

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: `[..., cursor]` -> `[...]` with `a2` set, no stack effect with it clear;
  jump to `a0` on a miss with nothing popped.
- **Semantics**: The cursor at depth `a1` must be at its end: `nil` for a pair spine,
  the length for a sequence. An improper tail is a miss. With `a2` set the
  cursor is popped, its extent ending here; the compiler places it on the top
  at that point. With `a2` clear it stays, as the standalone matcher's root
  keeps it for `MATCH_CAPTURES`.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
