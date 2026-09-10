# KOS_OP_SWITCH_EQ
- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Compares `left == right` with
  the same equality semantics as [CHECK_EQ](kos-op-check-compare.md)
  (`PyObject_RichCompareBool` with `Py_EQ`).
  - **Match**: pop both operands; `pc = arg`. Stack after: `[...]`.
  - **Mismatch**: pop `right` only; fall through. Stack after: `[..., left]`.
- **Invariants**: Jump target is the matching body. After a mismatch, bytecode
  must leave `left` correct for the next comparison (push a new `right`) or
  reach cleanup that drops `left`.
- **Compiler form:** Pre-compile arg = string label (body); post-compile arg =
  pc.

**Example:** Two-arm chain then else. Pool: 0 = 10, 1 = 1, 2 = 20, 3 = 2, 4 =
0. Discriminant 10: first key 1 (mismatch — pop key, fall through with `[10]`);
second key 20 (match — pop both, jump to body); body returns 2. Else drops
disc and returns 0. Covered by `tests/lib/opcode/test_switch.py`.

| index | opcode    | arg | arg view  | comment |
|------:|-----------|-----|-----------|---------|
| 0     | CONST     | 0   | 0 (10)    | stack: [..., 10] |
| 1     | CONST     | 1   | 1 (1)     | stack: [..., 10, 1] |
| 2     | SWITCH_EQ | 8   | 8 (-> 8)  | match → pop2, pc=8 (body); mismatch → [..., 10], next insn |
| 3     | CONST     | 2   | 2 (20)    | stack: [..., 10, 20] |
| 4     | SWITCH_EQ | 8   | 8 (-> 8)  | match → body; mismatch → [..., 10], next insn |
| 5     | POP       | 1   | 1         | else: drop discriminant |
| 6     | CONST     | 4   | 4 (0)     | else result |
| 7     | RETURN    | 1   | 1         | |
| 8     | LABEL     | —   | —         | body |
| 9     | CONST     | 3   | 3 (2)     | stack: [..., 2] |
| 10    | RETURN    | 1   | 1         | |

