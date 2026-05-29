# KOS_OP_CHECK_IS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left is not right, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Identity check (e.g. for None or singleton). Pool: 0 = [], 1 = [], 2 = 1, 3 = 0.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([])   | stack: [..., left] |
| 1     | CONST    | 1   | 1 ([])   | stack: [..., left, right] (different object) |
| 2     | CHECK_IS | 5   | 5 (-> 5) | if left is not right → pc=5 |
| 3     | CONST    | 2   | 2 (1)    | stack: [..., 1] |
| 4     | RETURN   | 1   | 1        | |
| 5     | LABEL    | —   | —        | diff |
| 6     | CONST    | 3   | 3 (0)    | stack: [..., 0] |
| 7     | RETURN   | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
