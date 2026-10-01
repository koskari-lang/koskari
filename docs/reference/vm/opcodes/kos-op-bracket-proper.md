# KOS_OP_BRACKET_PROPER
- **Style**: COUNT. Arg = number of element values on stack.
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result is the
  proper bracket list `[e_0 e_1 ... e_{count-1}]`. bracket-nil is implicitly
  appended as the tail. count 0 → push bracket-nil directly.
- **Invariants**: Each `e_i` may be any Python object; none need be a pair.
- **Use-case**: Emitted by `compile/bracket-proper`, and by the quasiquote
  compiler for the segments of a `[ ]` form. The twin of
  [PROPER](kos-op-proper.md): equivalent to BRACKET_CONS with an extra
  `CONST bracket-nil` on the stack, one instruction shorter.

**Example:** Build the bracket list `[a b c]` from three constants.

| index | opcode         | arg | arg view   | comment |
|------:|----------------|-----|------------|---------|
| 0     | CONST          | 0   | 0 (sym a)  | stack: [..., a] |
| 1     | CONST          | 1   | 1 (sym b)  | stack: [..., a, b] |
| 2     | CONST          | 2   | 2 (sym c)  | stack: [..., a, b, c] |
| 3     | BRACKET_PROPER | 3   | 3          | stack: [..., [a b c]] |
