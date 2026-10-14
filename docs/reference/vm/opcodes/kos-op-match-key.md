# KOS_OP_MATCH_KEY_CONST / KOS_OP_MATCH_KEY_VAR / KOS_OP_MATCH_KEY_FAST

- **Style**: EXTRA (LABEL, key)
- **Stack**: `[..., elem]` -> `[..., elem, value]`; jump to `a0` with nothing
  pushed on a miss.
- **Semantics**: Peek. Push the value under the key in the mapping `elem`: the
  constant `a1` on the CONST form, the value of slot `a1` otherwise. The
  mapping is probed without inserting, so a `defaultdict` neither inserts nor
  invents the key. An absent key is a miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
