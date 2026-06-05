# KOS_OP_CHECK_NE
- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left == right (check fails), jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when left == right (e.g. "not equal" guard). Pool: 0 = 1, 1 = 1, 2 = 0.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (1)    | stack: [..., left] |
| 1     | CONST    | 1   | 1 (1)    | stack: [..., left, right] |
| 2     | CHECK_NE | 5   | 5 (-> 5) | if left == right → pc=5; else stack: [...] |
| 3     | RETURN   | 1   | 1        | |
| 5     | LABEL    | —   | —        | fail |

