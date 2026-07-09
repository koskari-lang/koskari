# KOS_OP_CHECK_GE
- **Style**: EXTRA `(COUNT, LABEL)`. `a0` = operand count (≥ 2); `a1` = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., v0, ..., v_{n-1}]` (`v_{n-1}` = TOS, `n = a0`). Chain-compares adjacent pairs with `>=`; if the chain fails, jump to `a1`; else fall through. All `n` operands popped either way. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = `(count, string label, None, None)`; post-compile `a1` = pc.
