# KOS_OP_MATCH_CLASS_VAR / KOS_OP_MATCH_CLASS_FAST

- **Style**: EXTRA (LABEL, slot, RAW)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: Peek. `elem` must be an instance of the class in slot `a1`.
  When `a2`, the positional field count, is more than zero, the class's
  `__match_args__` must be a tuple with at least that many names, or the
  instruction raises `TypeError` as the Python `match` statement does. On
  Python 3.9 the tuple is synthesized for a dataclass or a namedtuple.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
