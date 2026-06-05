# KOS_OP_CHECK_IS_NOT
- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left is right, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when left is right (e.g. "is not" guard). Pool: 0 = None, 1 = None.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (None) | stack: [..., left] |
| 1     | CONST      | 1   | 1 (None) | stack: [..., left, right] |
| 2     | CHECK_IS_NOT | 5  | 5 (-> 5) | if left is right → pc=5; else stack: [...] |
| 3     | RETURN     | 1   | 1        | |
| 5     | LABEL      | —   | —        | same |

