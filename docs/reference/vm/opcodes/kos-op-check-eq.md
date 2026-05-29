# KOS_OP_CHECK_EQ

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left != right, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label (fail/mismatch target); post-compile arg = pc.

**Example:** Pattern match or guard: jump if not equal. Pool: 0 = 1, 1 = 2, 2 = 99, 3 = 0. Covered by `tests/lib/test_ceval.py`: `test_check_eq_jumps_on_mismatch`, `test_check_eq_continues_on_match`.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 (1)    | stack: [..., left] |
| 1     | CONST   | 1   | 1 (2)    | stack: [..., left, right] |
| 2     | CHECK_EQ| 5   | 5 (-> 5) | if left != right → pc=5; else stack: [...] |
| 3     | CONST   | 2   | 2 (99)   | stack: [..., 99] |
| 4     | RETURN  | 1   | 1        | |
| 5     | LABEL   | —   | —        | fail |
| 6     | CONST   | 3   | 3 (0)    | stack: [..., 0] |
| 7     | RETURN  | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
