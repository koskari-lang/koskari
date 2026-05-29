# KOS_OP_PROPER

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of element values on stack.
- **Status**: Implemented. (`koskari/lib/opcode/PROPER.c`)
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result is the
  proper list `(e_0 e_1 ... e_{count-1})`. nil is implicitly appended as the
  tail. count 0 → push nil directly.
- **Invariants**: Each `e_i` may be any Python object; none need be a pair.
- **Use-case**: Emitted by the quasiquote compiler for segments of non-splice
  elements. Equivalent to CONS with an extra `CONST nil` on the stack, but more
  compact: saves one instruction per list.

**Example:** Build the proper list `(a b c)` from three constants.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (sym a)  | stack: [..., a] |
| 1     | CONST  | 1   | 1 (sym b)  | stack: [..., a, b] |
| 2     | CONST  | 2   | 2 (sym c)  | stack: [..., a, b, c] |
| 3     | PROPER | 3   | 3          | stack: [..., (a b c)] |

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
