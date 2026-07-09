# KOS_OP_CHECK_EQ
- **Style**: EXTRA `(COUNT, LABEL)`. `a0` = operand count (≥ 2); `a1` = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., v0, ..., v_{n-1}]` (`v_{n-1}` = TOS, `n = a0`). Chain-compares adjacent pairs with `==`; if the chain fails (any pair unequal), jump to `a1`; else fall through. All `n` operands popped either way. Stack after: `[...]`.
- **Invariants**: Jump locality for target. Uses `PyObject_RichCompareBool` (identity fast-path), matching the pre-fusion CHECK behavior.
- **Compiler form:** Pre-compile arg = `(count, string label, None, None)` (label = fail/mismatch target); post-compile `a1` = pc.

**Example:** Pattern match or guard: jump if not equal. Pool: 0 = 1, 1 = 2, 2 = 99, 3 = 0. Covered by `tests/lib/opcode/test_checks.py`: `test_check_eq_jumps_on_mismatch`, `test_check_eq_continues_on_match`.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 (1)    | stack: [..., v0] |
| 1     | CONST   | 1   | 1 (2)    | stack: [..., v0, v1] |
| 2     | CHECK_EQ| (2, "fail") | (n=2, -> 5) | if v0 != v1 → pc=5; else stack: [...] |
| 3     | CONST   | 2   | 2 (99)   | stack: [..., 99] |
| 4     | RETURN  | 1   | 1        | |
| 5     | LABEL   | —   | —        | fail |
| 6     | CONST   | 3   | 3 (0)    | stack: [..., 0] |
| 7     | RETURN  | 1   | 1        | |
