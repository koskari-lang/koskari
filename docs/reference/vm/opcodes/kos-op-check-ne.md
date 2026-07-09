# KOS_OP_CHECK_NE
- **Style**: EXTRA `(COUNT, LABEL)`. `a0` = operand count (≥ 2); `a1` = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., v0, ..., v_{n-1}]` (`v_{n-1}` = TOS, `n = a0`). Chain-compares adjacent pairs with `!=`; if the chain fails, jump to `a1`; else fall through. All `n` operands popped either way. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = `(count, string label, None, None)`; post-compile `a1` = pc.

**Example:** Jump when the chain fails (e.g. "not equal" guard). Pool: 0 = 1, 1 = 1, 2 = 0.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (1)    | stack: [..., v0] |
| 1     | CONST    | 1   | 1 (1)    | stack: [..., v0, v1] |
| 2     | CHECK_NE | (2, "fail") | (n=2, -> 5) | if v0 == v1 → pc=5; else stack: [...] |
| 3     | RETURN   | 1   | 1        | |
| 5     | LABEL    | —   | —        | fail |
